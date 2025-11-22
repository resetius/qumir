// IO runtime shims: provide input/output functions expected by the program
// and allow embedders to inject their own input/output streams. Also exposes
// helpers to bind WebAssembly memory and reset IO state between runs.

let MEMORY = null; // WebAssembly.Memory (bound after instantiation)
const decoder = typeof TextDecoder !== 'undefined' ? new TextDecoder('utf-8') : null;

export function __bindMemory(mem) { MEMORY = mem; }

const NullInputStream = {
  readToken() { return '0'; },
  reset() {}
};
const NullOutputStream = {
  write() {},
  clear() {}
};

let INPUT_STREAM = NullInputStream;
let OUTPUT_STREAM = NullOutputStream;

function normalizeInputStream(stream) {
  if (!stream) return NullInputStream;
  if (typeof stream === 'function') {
    return { readToken: stream, reset: () => {} };
  }
  const read = (typeof stream.readToken === 'function')
    ? stream.readToken.bind(stream)
    : (typeof stream.nextToken === 'function')
      ? stream.nextToken.bind(stream)
      : null;
  if (!read) throw new Error('Input stream must provide readToken() or nextToken().');
  const reset = (typeof stream.reset === 'function') ? stream.reset.bind(stream)
    : (typeof stream.rewind === 'function') ? stream.rewind.bind(stream)
    : (() => {});
  return { readToken: read, reset };
}

function normalizeOutputStream(stream) {
  if (!stream) return NullOutputStream;
  const writeMethod = (typeof stream.write === 'function') ? stream.write
    : (typeof stream.append === 'function') ? stream.append
    : (typeof stream.print === 'function') ? stream.print
    : null;
  if (!writeMethod) throw new Error('Output stream must provide write()/append()/print().');
  const clearMethod = (typeof stream.clear === 'function') ? stream.clear
    : (typeof stream.reset === 'function') ? stream.reset
    : null;
  return {
    write: (...args) => writeMethod.apply(stream, args),
    clear: clearMethod ? (...args) => clearMethod.apply(stream, args) : NullOutputStream.clear
  };
}

function log(...a){ process.stderr.write(a.join(' ') + '\n'); }

export function setInputStream(stream) {
  INPUT_STREAM = normalizeInputStream(stream);
}

export function setOutputStream(stream) {
  OUTPUT_STREAM = normalizeOutputStream(stream);
}

export function __resetIO(clearStdout = false) {
  if (INPUT_STREAM && typeof INPUT_STREAM.reset === 'function') {
    try { INPUT_STREAM.reset(); } catch {}
  }
  if (clearStdout && OUTPUT_STREAM && typeof OUTPUT_STREAM.clear === 'function') {
    try { OUTPUT_STREAM.clear(); } catch {}
  }
}

function nextToken() {
  try {
    const value = INPUT_STREAM && typeof INPUT_STREAM.readToken === 'function'
      ? INPUT_STREAM.readToken()
      : '0';
    return (value === undefined || value === null || value === '') ? '0' : String(value);
  } catch {
    return '0';
  }
}

function appendStdout(text) {
  try {
    if (OUTPUT_STREAM && typeof OUTPUT_STREAM.write === 'function') {
      OUTPUT_STREAM.write(String(text));
    }
  } catch {}
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

export function output_double(x) {
  appendStdout(String(x));
}
export function output_int64(x) {
  appendStdout(BigInt(x).toString());
}
export function output_bool(x) { appendStdout(x ? "да" : "нет"); }
export function output_symbol(x) {
  // convert 32-bit unicode to string
  const codePoint = Number(x) >>> 0;
  let str = '';
  if (codePoint <= 0xFFFF) {
    str = String.fromCharCode(codePoint);
  } else if (codePoint <= 0x10FFFF) {
    const cp = codePoint - 0x10000;
    const highSurrogate = 0xD800 + ((cp >> 10) & 0x3FF);
    const lowSurrogate = 0xDC00 + (cp & 0x3FF);
    str = String.fromCharCode(highSurrogate, lowSurrogate);
  } else {
    str = '\uFFFD'; // Replacement character for invalid code points
  }
  appendStdout(str);
}
export function output_string(v) {
  // Support both: C-string pointer OR handle returned by string runtime
  const n = Number(v);
  // Try consulting string runtime if available (ESM import binding)
  if (!MEMORY || !decoder) {
    appendStdout(''); return;
  }
  const u8 = new Uint8Array(MEMORY.buffer);
  let p = n >>> 0;
  const start = p;
  while (p < u8.length && u8[p] !== 0) p++;
  appendStdout(decoder.decode(u8.subarray(start, p)));
  return;
}
