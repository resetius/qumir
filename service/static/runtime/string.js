"use strict";

// JS string runtime working with pointers (i64 params/returns are BigInt)
// - Inputs are C-string pointers in WASM memory (passed as BigInt/Number)
// - Concats allocate into a scratch arena in linear memory and return pointer (BigInt)
// - retain/release are no-ops under JS GC

let MEMORY = null; // WebAssembly.Memory
const PAGE = 65536; // 64 KiB
const encoder = typeof TextEncoder !== 'undefined' ? new TextEncoder() : null;
const decoder = typeof TextDecoder !== 'undefined' ? new TextDecoder('utf-8') : null;

let SCRATCH_START = 0;
let SCRATCH_END = 0;
let scratchPtr = 0;

export function __bindMemory(mem) { MEMORY = mem; _rebindScratch(); }
export function __resetStrings() { if (MEMORY) _rebindScratch(); }

function _rebindScratch() {
    const size = MEMORY ? MEMORY.buffer.byteLength : 0;
    SCRATCH_END = size;
    SCRATCH_START = Math.max(0, size - PAGE);
    scratchPtr = SCRATCH_START;
}

function _ensureScratch(n) {
    if (!MEMORY) return false;
    while (scratchPtr + n + 1 > SCRATCH_END) {
        MEMORY.grow(1);
        _rebindScratch();
    }
    return true;
}

function _normalizePtr(v) {
    // Inputs come as BigInt (i64). Keep low 32 bits for addresses.
    if (typeof v === 'bigint') return Number(v & 0xFFFFFFFFn) >>> 0;
    return Number(v) >>> 0;
}

function readCString(v) {
    if (!MEMORY || !decoder) return '';
    const p0 = _normalizePtr(v);
    const u8 = new Uint8Array(MEMORY.buffer);
    let p = p0;
    while (p < u8.length && u8[p] !== 0) p++;
    return decoder.decode(u8.subarray(p0, p));
}

function writeStrToScratch(s) {
    if (!MEMORY || !encoder) return 0;
    const bytes = encoder.encode(String(s));
    if (!_ensureScratch(bytes.length)) return 0;
    const u8 = new Uint8Array(MEMORY.buffer);
    const p = scratchPtr >>> 0;
    u8.set(bytes, p);
    u8[p + bytes.length] = 0;
    scratchPtr = p + bytes.length + 1;
    return p;
}

function writeI8(ptr, value) {
    if (!MEMORY) return;
    const p = _normalizePtr(ptr);
    const u8 = new Uint8Array(MEMORY.buffer);
    if (p >= 0 && p < u8.length) {
        u8[p] = value & 0xFF;
    }
}

// Runtime API (pointer-based)
// Copy a literal C-string from module data segment into scratch arena to simulate heap allocation
export function str_from_lit(ptr) {
    const p = _normalizePtr(ptr);
    if (!MEMORY || !decoder) return p; // fallback: original pointer
    const u8 = new Uint8Array(MEMORY.buffer);
    let end = p;
    while (end < u8.length && u8[end] !== 0) end++;
    const slice = u8.subarray(p, end);
    const text = decoder.decode(slice);
    return writeStrToScratch(text);
}
export function str_retain(_ptr) {}
export function str_release(_ptr) {}
export function str_concat(a, b) { return writeStrToScratch(readCString(a) + readCString(b)); }
export function str_slice(strPtr, startSymbol, endSymbol) {
    // 1-indexed, endSymbol is inclusive
    const s = readCString(strPtr);
    const symbols = Array.from(s);
    const len = symbols.length;
    // Convert from 1-indexed to 0-indexed
    const start = Math.max(0, Math.min(len, Number(startSymbol) - 1));
    let endInc = Number(endSymbol) - 1;
    if (!Number.isFinite(endInc)) endInc = len - 1;
    endInc = Math.max(-1, Math.min(len - 1, endInc));
    if (start > endInc) return writeStrToScratch('');
    const out = symbols.slice(start, endInc + 1).join('');
    return writeStrToScratch(out);
}
export function str_compare(a, b) {
    const sa = readCString(a);
    const sb = readCString(b);
    if (sa === sb) return BigInt(0);
    return sa < sb ? BigInt(-1) : BigInt(1);
}
export function str_len(ptr) {
    const s = readCString(ptr);
    return BigInt(Array.from(s).length);
}
export function str_unicode(ptr) {
    const s = readCString(ptr);
    const symbols = Array.from(s);
    if (symbols.length === 0) return BigInt(0);
    return BigInt(symbols[0].codePointAt(0));
}
// Return codepoint of symbol at 1-based position `pos` or -1 if out of range (matches C++ str_symbol_at semantics returning int32)
export function str_symbol_at(strPtr, pos) {
    const s = readCString(strPtr);
    if (!s) return -1;
    const symbols = Array.from(s);
    const p = Number(pos);
    if (!Number.isFinite(p) || p < 1 || p > symbols.length) return -1;
    return symbols[p - 1].codePointAt(0);
}
export function str_from_unicode(codepoint) {
    const cp = Number(codepoint);
    if (!Number.isFinite(cp) || cp < 0 || cp > 0x10FFFF) return writeStrToScratch('');
    return writeStrToScratch(String.fromCodePoint(cp));
}
export function str_str(haystackPtr, needlePtr) {
    const haystack = readCString(haystackPtr);
    const needle = readCString(needlePtr);
    const index = haystack.indexOf(needle);
    // need 1-indexed symbol position, 0 if not found
    if (index === -1) return BigInt(0);
    let symbolIndex = 0;
    for (let i = 0; i < index; ) {
        const cp = haystack.codePointAt(i);
        i += cp > 0xFFFF ? 2 : 1;
        symbolIndex++;
    }
    symbolIndex++; // convert to 1-based
    return BigInt(symbolIndex);
}
export function str_str_from(startSymbolPos, haystackPtr, needlePtr) {
    const startPos = Number(startSymbolPos);
    if (!Number.isFinite(startPos) || startPos < 1) return BigInt(0);
    const haystack = readCString(haystackPtr);
    const needle = readCString(needlePtr);
    // Convert symbol position to string index
    let symbolIndex = 1;
    let strIndex = 0;
    // Walk symbols to find byte/JS index that corresponds to startPos (1-based symbol index)
    while (strIndex < haystack.length && symbolIndex < startPos) {
        const cp = haystack.codePointAt(strIndex);
        strIndex += cp > 0xFFFF ? 2 : 1;
        symbolIndex++;
    }
    const index = haystack.indexOf(needle, strIndex);
    // need 1-indexed symbol position (from start of string), 0 if not found
    if (index === -1) return BigInt(0);

    // Count symbols from beginning of haystack up to the found index,
    // same semantics as str_str: position in characters, 1-based.
    let symbolPos = 0;
    for (let i = 0; i < index; ) {
        const cp = haystack.codePointAt(i);
        i += cp > 0xFFFF ? 2 : 1;
        symbolPos++;
    }
    symbolPos++; // convert to 1-based
    return BigInt(symbolPos);
}

// Numeric conversions / formatting (mirror C++ runtime semantics)
export function str_from_double(x) {
    // C++ uses std::ostringstream with precision(15) and default format
    // toPrecision(15) matches that for most values (scientific for extremes)
    const num = Number(x);
    return writeStrToScratch(num.toPrecision(15));
}

export function str_from_int(x) {
    // Accept Number or BigInt and produce decimal string
    if (typeof x === 'bigint') return writeStrToScratch(x.toString());
    const n = Math.trunc(Number(x));
    return writeStrToScratch(String(n));
}

export function str_to_double(strPtr, outOkPtr) {
    const s = readCString(strPtr);
    if (!s) {
        if (outOkPtr) writeI8(outOkPtr, 0);
        return 0.0;
    }
    const v = Number(s.trim());
    const ok = Number.isFinite(v);
    if (outOkPtr) writeI8(outOkPtr, ok ? 1 : 0);
    return ok ? v : 0.0;
}

export function str_to_int(strPtr, outOkPtr) {
    const s = readCString(strPtr);
    if (!s) {
        if (outOkPtr) writeI8(outOkPtr, 0);
        return BigInt(0);
    }
    // Trim and validate integer format (optional leading sign + digits)
    const t = s.trim();
    const m = /^[-+]?\d+$/.test(t);
    if (!m) {
        if (outOkPtr) writeI8(outOkPtr, 0);
        return BigInt(0);
    }
    try {
        const bi = BigInt(t);
        if (outOkPtr) writeI8(outOkPtr, 1);
        return bi;
    } catch {
        if (outOkPtr) writeI8(outOkPtr, 0);
        return BigInt(0);
    }
}