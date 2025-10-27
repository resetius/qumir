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
    // endSymbol is inclusive
    const s = readCString(strPtr);
    const symbols = Array.from(s);
    const len = symbols.length;
    const start = Math.max(0, Math.min(len, Number(startSymbol)));
    let endInc = Number(endSymbol);
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
