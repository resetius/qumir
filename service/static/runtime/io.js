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
const NullFileManager = {
  open() { return -1; },
  close() {},
  hasMore() { return false; },
  getStream() { return null; },
  reset() {}
};

let INPUT_STREAM = NullInputStream;
let DEFAULT_INPUT_STREAM = NullInputStream;
let DEFAULT_INPUT_INITIALIZED = false;
let OUTPUT_STREAM = NullOutputStream;
let FILE_MANAGER = NullFileManager;

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

function applyInputStream(stream, { updateDefault = false } = {}) {
  const normalized = normalizeInputStream(stream);
  INPUT_STREAM = normalized;
  if (updateDefault || !DEFAULT_INPUT_INITIALIZED) {
    DEFAULT_INPUT_STREAM = normalized;
    DEFAULT_INPUT_INITIALIZED = true;
  }
  return normalized;
}

function normalizeFileManager(manager) {
  if (!manager || typeof manager !== 'object') {
    return NullFileManager;
  }
  const open = typeof manager.open === 'function' ? manager.open.bind(manager) : NullFileManager.open;
  const close = typeof manager.close === 'function' ? manager.close.bind(manager) : NullFileManager.close;
  const hasMore = typeof manager.hasMore === 'function' ? manager.hasMore.bind(manager) : NullFileManager.hasMore;
  const getStream = typeof manager.getStream === 'function' ? manager.getStream.bind(manager) : NullFileManager.getStream;
  const reset = typeof manager.reset === 'function' ? manager.reset.bind(manager) : NullFileManager.reset;
  return { open, close, hasMore, getStream, reset };
}

function readCString(ptr) {
  if (!MEMORY || !decoder) return '';
  const buffer = new Uint8Array(MEMORY.buffer);
  let offset = Number(ptr) >>> 0;
  if (offset >= buffer.length) return '';
  let end = offset;
  while (end < buffer.length && buffer[end] !== 0) {
    end++;
  }
  return decoder.decode(buffer.subarray(offset, end));
}

function log(...a){
  const msg = a.join(' ') + '\n';
  if (typeof process !== 'undefined' && process.stderr) {
    process.stderr.write(msg);
  } else if (typeof console !== 'undefined') {
    console.error(msg);
  }
}

export function setInputStream(stream, options = {}) {
  const updateDefault = Object.prototype.hasOwnProperty.call(options, 'makeDefault')
    ? !!options.makeDefault
    : true;
  applyInputStream(stream, { updateDefault });
}

export function setOutputStream(stream) {
  OUTPUT_STREAM = normalizeOutputStream(stream);
}

export function setFileManager(manager) {
  if (FILE_MANAGER && typeof FILE_MANAGER.reset === 'function') {
    try { FILE_MANAGER.reset(); } catch {}
  }
  FILE_MANAGER = normalizeFileManager(manager);
}

export function __resetIO(clearStdout = false) {
  if (INPUT_STREAM && typeof INPUT_STREAM.reset === 'function') {
    try { INPUT_STREAM.reset(); } catch {}
  }
  INPUT_STREAM = DEFAULT_INPUT_STREAM || NullInputStream;
  if (clearStdout && OUTPUT_STREAM && typeof OUTPUT_STREAM.clear === 'function') {
    try { OUTPUT_STREAM.clear(); } catch {}
  }
  if (FILE_MANAGER && typeof FILE_MANAGER.reset === 'function') {
    try { FILE_MANAGER.reset(); } catch {}
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
  appendStdout(readCString(v));
}

function asHandle(value) {
  return Number(value) | 0;
}

export function file_open_for_read(ptr) {
  try {
    const name = readCString(ptr);
    if (!name) return -1;
    const handle = FILE_MANAGER.open(name);
    return Number.isInteger(handle) ? (handle | 0) : -1;
  } catch {
    return -1;
  }
}

export function file_close(handle) {
  try {
    FILE_MANAGER.close(asHandle(handle));
  } catch {}
}

export function file_has_more_data(handle) {
  try {
    return FILE_MANAGER.hasMore(asHandle(handle)) ? 1 : 0;
  } catch {
    return 0;
  }
}

export function input_set_file(handle) {
  try {
    const stream = FILE_MANAGER.getStream(asHandle(handle));
    if (stream) {
      applyInputStream(stream, { updateDefault: false });
    }
  } catch {}
}

export function input_reset_file() {
  INPUT_STREAM = DEFAULT_INPUT_STREAM || NullInputStream;
}
