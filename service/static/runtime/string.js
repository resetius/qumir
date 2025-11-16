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

// Runtime API (pointer-based)
export function str_from_lit(ptr) { return _normalizePtr(ptr); }
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
    if (sa === sb) return 0;
    return sa < sb ? -1 : 1;
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
    while (strIndex < haystack.length && symbolIndex < startPos) {
        const cp = haystack.codePointAt(strIndex);
        strIndex += cp > 0xFFFF ? 2 : 1;
        symbolIndex++;
    }
    const index = haystack.indexOf(needle, strIndex);
    // need 1-indexed symbol position, 0 if not found
    if (index === -1) return BigInt(0);
    symbolIndex = 1;
    for (let i = 0; i < index; ) {
        const cp = haystack.codePointAt(i);
        i += cp > 0xFFFF ? 2 : 1;
        symbolIndex++;
    }
    symbolIndex++; // convert to 1-based
    return BigInt(strIndex + symbolIndex);
}