// IO runtime shims: provide input/output functions expected by the program
// and bridge them to the web UI (#stdin and #stdout). Also exposes helpers
// to bind WebAssembly memory and reset IO state between runs.

let MEMORY = null; // WebAssembly.Memory (bound after instantiation)
const decoder = typeof TextDecoder !== 'undefined' ? new TextDecoder('utf-8') : null;

export function __bindMemory(mem) { MEMORY = mem; }

function q$(sel) { return (typeof document !== 'undefined') ? document.querySelector(sel) : null; }

function appendStdout(text) {
  const el = q$('#stdout');
  if (!el) return;
  el.textContent += String(text);
}

let inputTokens = null;
let inputIndex = 0;
export function __resetIO(clearStdout = false) {
  inputTokens = null;
  inputIndex = 0;
  if (clearStdout) {
    const el = q$('#stdout');
    if (el) el.textContent = '';
  }
}

function nextToken() {
  if (!inputTokens) {
    const el = q$('#stdin');
    const content = el ? String(el.value || '') : '';
    inputTokens = content.match(/\S+/g) || [];
    inputIndex = 0;
  }
  if (inputIndex < inputTokens.length) return inputTokens[inputIndex++];
  return '0';
}

// Runtime IO used by programs
export function input_double() {
  const t = nextToken();
  const v = Number.parseFloat(t);
  return Number.isFinite(v) ? v : 0;
}

export function input_int64() {
  const t = nextToken();
  try { return BigInt(t); } catch { return 0n; }
}

export function output_double(x) { appendStdout(String(x)); }
export function output_int64(x) { appendStdout(BigInt(x).toString()); }
export function output_string(v) {
  // Support both: C-string pointer OR handle returned by string runtime
  const n = Number(v);
  // Try consulting string runtime if available (ESM import binding)
  if (!MEMORY || !decoder) { appendStdout(''); return; }
  const u8 = new Uint8Array(MEMORY.buffer);
  let p = n >>> 0;
  const start = p;
  while (p < u8.length && u8[p] !== 0) p++;
  appendStdout(decoder.decode(u8.subarray(start, p)));
  return;
}
