"use strict";

// Minimal array runtime for WASM linear memory
// - array_create(size: i32|i64) -> i32 pointer to zeroed block
// - array_destroy(ptr: i32|i64) -> no-op (bump-only allocator)
// Strategy: grow memory per allocation and place the block at the previous end.
// This avoids coordination with other arenas and guarantees non-overlap.

let MEMORY = null; // WebAssembly.Memory
const PAGE = 65536; // 64 KiB

export function __bindMemory(mem) { MEMORY = mem; }
export function __resetArrays() { /* no persistent state to reset */ }

function toU32(v) {
  return (typeof v === 'bigint') ? Number(v & 0xFFFFFFFFn) >>> 0 : Number(v) >>> 0;
}

function pagesFor(bytes) {
  return Math.ceil(bytes / PAGE);
}

// Allocate a zeroed block of given size (in bytes) and return its pointer.
export function array_create(size) {
  if (!MEMORY) return 0;
  const n = toU32(size);
  if (n === 0) return 0;
  const oldBytes = MEMORY.buffer.byteLength >>> 0;
  const need = n;
  const extra = pagesFor(need);
  // Grow memory by the required number of pages; new buffer will be larger.
  if (extra > 0) MEMORY.grow(extra);
  // After grow, buffer is reallocated; zero the allocated region.
  const u8 = new Uint8Array(MEMORY.buffer);
  // Newly grown area starts at oldBytes and is zero-initialized per spec in most engines,
  // but we explicitly zero to be safe and to support engines that don't.
  // Note: If engine already zeroes, this is still correct and cheap in JS.
  u8.fill(0, oldBytes, oldBytes + need);
  return oldBytes >>> 0;
}

// Free is a no-op for bump-only allocator.
export function array_destroy(_ptr) { /* intentionally empty */ }

export function array_str_destroy(ptr, arraySize) {
  // No-op under JS GC; signature exists to match lowered calls.
}