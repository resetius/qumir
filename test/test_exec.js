#!/usr/bin/env node
// JS execution test harness for .kum programs compiled to WebAssembly via `qumirc --wasm`.
// Focus: execution only (analogous to Exec* tests in test_reg.cpp) comparing return value and stdout against goldens.
// Usage:
//   node test/test_exec.js [--root test/regtest] [--update] [--print] [--wasm-dir build/wasm]
// Environment overrides:
//   QUMIRC   path to qumirc/qumiri compiler (default tries bin/qumirc then bin/qumiri)
// Notes:
//   Each .kum file should define exactly one top-level algorithm (after splitting: one file = one function).
//   Goldens: .result for return value, optional .result.stdout for printed output.

const fs = require('fs');
const path = require('path');
const cp = require('child_process');

function log(...a){ process.stderr.write(a.join(' ') + '\n'); }

let rootDir = 'test/regtest';
let update = false;
let printOutput = false;
let wasmDir = 'build/wasm';
let runtimeDir = null; // directory with JS runtime host functions
let filterPattern = null; // optional glob-style filter for case names (similar to gtest_filter)
let xmlOutputPath = null; // optional path to write JUnit-style XML report
for (let i=2;i<process.argv.length;i++) {
  const arg = process.argv[i];
  if (arg === '--root' && i+1 < process.argv.length) { rootDir = process.argv[++i]; }
  else if (arg === '--update') update = true;
  else if (arg === '--print') printOutput = true;
  else if (arg === '--wasm-dir' && i+1 < process.argv.length) { wasmDir = process.argv[++i]; }
  else if (arg === '--runtime' && i+1 < process.argv.length) { runtimeDir = process.argv[++i]; }
  else if (arg === '--filter' && i+1 < process.argv.length) { filterPattern = process.argv[++i]; }
  else if (arg === '--xml' && i+1 < process.argv.length) { xmlOutputPath = process.argv[++i]; }
}

const casesDir = path.join(rootDir, 'cases');
const goldensDir = path.join(rootDir, 'goldens');
// By convention, per-case stdin files live alongside the .kum sources
// under the same relative path inside the cases directory, with a
// `.stdin` extension, e.g. `cases/io/input.kum` + `cases/io/input.stdin`.
const stdinDir = casesDir;

// Provide a minimal DOM shim so runtime/io.js can append to #stdout when running under Node.
if (typeof global.document === 'undefined') {
  global.__stdoutEl = { textContent: '' };
  global.__stdinEl = { value: '' };
  global.document = {
    querySelector: (sel) => {
      if (sel === '#stdout') return global.__stdoutEl;
      if (sel === '#stdin') return global.__stdinEl;
      return null;
    }
  };
}

function resetDomStdout() {
  if (global.__stdoutEl) global.__stdoutEl.textContent = '';
}
function getDomStdout() {
  return global.__stdoutEl ? global.__stdoutEl.textContent : '';
}

function readAll(p) {
  return fs.readFileSync(p, 'utf8');
}
function writeAll(p, data) {
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, data);
  log('Updated golden:', p, 'bytes=', data.length);
}

function collectCases(dir) {
  const out = [];
  function walk(d){
    for (const e of fs.readdirSync(d)) {
      const full = path.join(d,e);
      const st = fs.statSync(full);
      if (st.isDirectory()) walk(full); else if (st.isFile() && e.endsWith('.kum')) {
        const rel = path.relative(dir, full);
        out.push(rel.slice(0, -4)); // strip .kum
      }
    }
  }
  walk(dir);
  out.sort();
  return out;
}

function loadStdinForCase(caseBase) {
  const stdinPath = path.join(stdinDir, caseBase + '.stdin');
  if (fs.existsSync(stdinPath)) {
    return readAll(stdinPath);
  }
  return null;
}

function findCompiler() {
  const env = process.env.QUMIRC;
  if (env && fs.existsSync(env)) return env;
  const candidates = [path.join('bin','qumirc'), path.join('bin','qumiri')];
  for (const c of candidates) if (fs.existsSync(c)) return c;
  throw new Error('Compiler not found (set QUMIRC or build bin/qumirc).');
}

function parseAlgHeader(code) {
  // Find first line starting with 'алг ' and return both declared return "type" token (if any) and name.
  // Examples:
  //   алг ф            -> { type: null, name: 'ф' }
  //   алг лит ф        -> { type: 'лит', name: 'ф' }
  //   алг цел f(парам) -> { type: 'цел', name: 'f' }
  const lines = code.split(/\r?\n/);
  for (const line of lines) {
    if (!/^\s*алг\s+/u.test(line)) continue;
    const trimmed = line.trim();
    const tokens = trimmed.split(/\s+/);
    if (tokens.length === 2) {
      // "алг <name>"
      return { type: null, name: tokens[1].replace(/\(.*/, '') };
    } else if (tokens.length >= 3) {
      // "алг <type> <name> ..."
      return { type: tokens[1], name: tokens[2].replace(/\(.*/, '') };
    }
  }
  return { type: null, name: null };
}

function compileCase(compiler, caseBase) {
  const srcPath = path.join(casesDir, caseBase + '.kum');
  const outPath = path.join(wasmDir, caseBase + '.wasm');
  fs.mkdirSync(path.dirname(outPath), { recursive: true });
  const args = [compiler, '--wasm', srcPath, '-o', outPath];
  const r = cp.spawnSync(args[0], args.slice(1), { stdio: 'inherit' });
  if (r.status !== 0) throw new Error('Compile failed for ' + srcPath);
  return outPath;
}

function utf8FromMemory(mem, ptr) {
  const bytes = new Uint8Array(mem.buffer);
  const out = [];
  for (let i=ptr; i<bytes.length; i++) { const b=bytes[i]; if (b===0) break; out.push(b); }
  return Buffer.from(out).toString('utf8');
}

// Minimal WASM parser to extract return type of an exported function by name.
// Supports: single-value returns (i32,i64,f32,f64). Ignores multi-value.
function wasmReturnType(bytes, exportName) {
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let pos = 0;
  function readU32() {
    let result = 0, shift = 0;
    while (true) {
      const b = bytes[pos++];
      result |= (b & 0x7F) << shift;
      if ((b & 0x80) === 0) break;
      shift += 7;
    }
    return result >>> 0;
  }
  function skip(n){ pos += n; }
  // Magic + version
  if (bytes.length < 8) return null;
  pos = 8;
  let types = []; // array of {returns:[valtype]}
  let funcTypeIndices = []; // per defined function
  let importFuncCount = 0;
  let exportFuncMap = new Map(); // name -> funcIdx
  while (pos < bytes.length) {
    const id = bytes[pos++];
    const size = readU32();
    const sectionStart = pos;
    if (id === 1) { // type
      const count = readU32();
      for (let i=0;i<count;i++) {
        const form = bytes[pos++]; // 0x60 func
        if (form !== 0x60) { return null; }
        const paramCount = readU32();
        for (let p=0;p<paramCount;p++) pos++; // skip params
        const retCount = readU32();
        const returns = [];
        for (let r=0;r<retCount;r++) returns.push(bytes[pos++]);
        types.push({ returns });
      }
    } else if (id === 2) { // import
      const count = readU32();
      for (let i=0;i<count;i++) {
        // module name
        const modLen = readU32(); pos += modLen;
        const fieldLen = readU32(); pos += fieldLen;
        const kind = bytes[pos++];
        if (kind === 0x00) { // func
          importFuncCount++;
          readU32(); // type index
        } else if (kind === 0x01) { // table
          pos++; // elem type
          const flags = readU32(); readU32(); if (flags & 0x01) readU32();
        } else if (kind === 0x02) { // memory
          const flags = readU32(); readU32(); if (flags & 0x01) readU32();
        } else if (kind === 0x03) { // global
          pos += 2; // type + mutability
        }
      }
    } else if (id === 3) { // function section
      const count = readU32();
      for (let i=0;i<count;i++) funcTypeIndices.push(readU32());
    } else if (id === 7) { // export
      const count = readU32();
      for (let i=0;i<count;i++) {
        const nameLen = readU32();
        const nameBytes = bytes.slice(pos, pos+nameLen);
        pos += nameLen;
        const name = Buffer.from(nameBytes).toString('utf8');
        const kind = bytes[pos++];
        const index = readU32();
        if (kind === 0x00) { // func
          exportFuncMap.set(name, index);
        }
      }
    }
    pos = sectionStart + size;
  }
  const funcIdx = exportFuncMap.get(exportName);
  if (funcIdx == null) return null;
  // Distinguish imported vs defined
  let typeIdx;
  if (funcIdx < importFuncCount) {
    // We skipped import type indices; need more parsing for exact type. Simplify: unknown.
    return null;
  } else {
    const definedIdx = funcIdx - importFuncCount;
    typeIdx = funcTypeIndices[definedIdx];
  }
  if (typeIdx == null || typeIdx >= types.length) return null;
  const returns = types[typeIdx].returns;
  if (!returns || returns.length === 0) return 'void';
  const t = returns[0];
  switch (t) {
    case 0x7F: return 'i32';
    case 0x7E: return 'i64';
    case 0x7D: return 'f32';
    case 0x7C: return 'f64';
    default: return null;
  }
}

// Parse wasm exports/imports and start section for diagnostics.
function wasmMetadata(bytes) {
  let pos = 8; // skip magic/version
  const imports = [];
  const exports = [];
  let startIndex = null;
  function readU32() {
    let result = 0, shift = 0;
    while (true) {
      const b = bytes[pos++];
      result |= (b & 0x7F) << shift;
      if ((b & 0x80) === 0) break;
      shift += 7;
    }
    return result >>> 0;
  }
  if (bytes.length < 8) return { imports, exports, startIndex };
  while (pos < bytes.length) {
    const id = bytes[pos++];
    const size = readU32();
    const sectionStart = pos;
    if (id === 2) { // import
      const count = readU32();
      for (let i=0;i<count;i++) {
        const modLen = readU32();
        const modName = Buffer.from(bytes.slice(pos, pos+modLen)).toString('utf8'); pos += modLen;
        const fieldLen = readU32();
        const fieldName = Buffer.from(bytes.slice(pos, pos+fieldLen)).toString('utf8'); pos += fieldLen;
        const kind = bytes[pos++];
        imports.push({ modName, fieldName, kind });
        // skip type specifics
        if (kind === 0x00) { readU32(); }
        else if (kind === 0x01) { pos++; const flags = readU32(); readU32(); if (flags & 0x01) readU32(); }
        else if (kind === 0x02) { const flags = readU32(); readU32(); if (flags & 0x01) readU32(); }
        else if (kind === 0x03) { pos += 2; }
      }
    } else if (id === 7) { // export
      const count = readU32();
      for (let i=0;i<count;i++) {
        const nameLen = readU32();
        const name = Buffer.from(bytes.slice(pos, pos+nameLen)).toString('utf8'); pos += nameLen;
        const kind = bytes[pos++];
        const index = readU32();
        exports.push({ name, kind, index });
      }
    } else if (id === 8) { // start
      startIndex = readU32();
    }
    pos = sectionStart + size;
  }
  return { imports, exports, startIndex };
}

function loadRuntimeFunctions(dir, memory) {
  const fns = {};
  if (!dir) return fns;
  if (!fs.existsSync(dir)) { log('[WARN] runtime directory not found:', dir); return fns; }
  function walk(d){
    for (const e of fs.readdirSync(d)) {
      const full = path.join(d,e);
      const st = fs.statSync(full);
      if (st.isDirectory()) walk(full); else if (st.isFile() && /\.m?js$/.test(e)) {
        try {
          const mod = require(full);
          // Allow a module export function map or a factory returning map when called with memory
          let exported = mod;
          if (typeof mod === 'function') {
            exported = mod(memory);
          } else if (mod && typeof mod.init === 'function') {
            mod.init(memory);
          }
          for (const [name, val] of Object.entries(exported)) {
            if (typeof val === 'function') {
              // if (fns[name]) log('[INFO] override runtime fn', name, 'from', full);
              fns[name] = val;
            }
          }
        } catch (e) {
          log('[WARN] failed to load runtime file', full, e.message);
        }
      }
    }
  }
  walk(dir);
  return fns;
}

function instantiateWasm(wasmPath, ioCapture, stdinContent) {
  const bytes = fs.readFileSync(wasmPath);
  // Create a provisional memory only if the module imports one; otherwise we'll switch to the module's own defined memory after instantiation.
  let memory = new WebAssembly.Memory({ initial: 32, maximum: 256 });
  const decoderCache = new Map();
  function readStr(ptr) {
    if (!ptr) return '';
    if (decoderCache.has(ptr)) return decoderCache.get(ptr);
    const s = utf8FromMemory(memory, ptr);
    decoderCache.set(ptr, s);
    return s;
  }
  // Common import names guesses; adjust if actual wasm expects different.
  const runtimeFns = loadRuntimeFunctions(runtimeDir, memory);
  const env = Object.assign({
    memory,
    // Generic output helpers
    io_write: (ptr, len) => {
      const bytes = new Uint8Array(memory.buffer, ptr, len);
      ioCapture.stdout += Buffer.from(bytes).toString('utf8');
    },
    print: (ptr) => { ioCapture.stdout += readStr(ptr); },
    putchar: (c) => { ioCapture.stdout += String.fromCharCode(Number(c) & 0xFF); },
    // Default input helpers; if the runtime doesn't provide them, we will
    // supply implementations that read from the per-case stdinContent.
    str_input: () => 0,
    input_int64: () => 0n,
  }, runtimeFns);
  // Always override input_int64 to consume tokens from a per-case .stdin file
  // (space/newline-separated). If no .stdin exists and the program attempts
  // to read, we throw to signal missing test input instead of silently
  // reading from DOM or returning 0.
  {
    let stdinTokens = null;
    let stdinIndex = 0;
    function ensureTokens() {
      if (stdinTokens !== null) return;
      if (stdinContent == null) {
        throw new Error('input_int64 requested but no .stdin file found for this test');
      }
      stdinTokens = String(stdinContent).match(/\S+/g) || [];
      stdinIndex = 0;
    }
    env.input_int64 = () => {
      ensureTokens();
      if (stdinIndex >= stdinTokens.length) {
        throw new Error('input_int64 requested more tokens than provided in .stdin');
      }
      const t = stdinTokens[stdinIndex++];
      try { return BigInt(t); } catch { return 0n; }
    };
  }
  // Bind memory for runtime modules that expose __bindMemory (e.g. io.js, string.js)
  if (env.__bindMemory && typeof env.__bindMemory === 'function') {
    try { env.__bindMemory(memory); if (printOutput) log('[INIT] __bindMemory called'); } catch (e) { if (printOutput) log('[WARN] __bindMemory failed', e.message); }
  }
  // Ensure required functions exist; supply safe stubs if missing
  const requiredStubs = [
    'array_create','array_destroy','array_str_destroy',
    'str_from_lit','str_retain','str_release','str_concat','str_compare','str_len',
    // IO/output helpers used by language 'вывод'
    'output_double','output_int64','output_bool','output_symbol','output_string'
  ];
  for (const name of requiredStubs) {
    if (!env[name]) {
      if (name === 'output_string') {
        // Do NOT auto-append newline; newlines appear as separate string literals ("\\n") in the program.
        const stub = (ptr) => { const s = readStr(Number(ptr) >>> 0); ioCapture.stdout += s; return 0; };
        stub.__isStub = true;
        env[name] = stub;
      } else if (name === 'output_double') {
        const stub = (x) => { ioCapture.stdout += String(x); return 0; };
        stub.__isStub = true;
        env[name] = stub;
      } else if (name === 'output_int64') {
        const stub = (x) => { ioCapture.stdout += (typeof x === 'bigint' ? x.toString() : String(x)); return 0; };
        stub.__isStub = true;
        env[name] = stub;
      } else if (name === 'output_bool') {
        env[name] = (x) => { ioCapture.stdout += (x ? 'да' : 'нет'); return 0; };
      } else if (name === 'output_symbol') {
        env[name] = (cp) => { const codePoint = Number(cp) >>> 0; ioCapture.stdout += String.fromCodePoint(codePoint); return 0; };
      } else {
        env[name] = (...args) => { /* stub: no-op */ if (printOutput) log('[STUB]', name, 'called args=', args); return 0; };
      }
      if (printOutput) log('[STUB-INSTALL]', name);
    }
  }
  // Wrap output_string for forced decode & tracing without double-adding text.
  if (env.output_string && !env.output_string.__forcedDecode) {
    const orig = env.output_string;
    env.output_string = function(ptr) {
      const p = Number(ptr) >>> 0;
      const decoded = readStr(p);
      if (printOutput) {
        const u8 = new Uint8Array(memory.buffer);
        let dump = [];
        for (let i=0;i<32;i++) { const b = u8[p+i]; dump.push(b === undefined ? '??' : b.toString(16).padStart(2,'0')); if (b === 0) break; }
        log('[DECODE] output_string ptr='+p+' text="'+decoded+'" bytes='+dump.join(' '));
      }
      // We already append inside the stub/original; avoid duplicate. If original is our stub, it will append.
      // If original implementation prints elsewhere (e.g., DOM), ensure we ALSO append locally to ioCapture.
      if (orig.__isStub !== true) {
        ioCapture.stdout += decoded;
      }
      return orig(ptr);
    };
    env.output_string.__forcedDecode = true;
  }
  // Wrap output_int64 to always capture numeric output even if runtime writes only to DOM.
  if (env.output_int64 && !env.output_int64.__forcedCapture) {
    const origInt = env.output_int64;
    env.output_int64 = function(x) {
      if (origInt.__isStub !== true) {
        const asStr = (typeof x === 'bigint' ? x.toString() : String(x));
        ioCapture.stdout += asStr;
      }
      return origInt(x);
    };
    env.output_int64.__forcedCapture = true;
  }
  // Wrap output_double to always capture floating output even if runtime writes only to DOM.
  if (env.output_double && !env.output_double.__forcedCapture) {
    const origDouble = env.output_double;
    env.output_double = function(x) {
      if (origDouble.__isStub !== true) {
        ioCapture.stdout += String(x);
      }
      return origDouble(x);
    };
    env.output_double.__forcedCapture = true;
  }
  // Wrap output_bool to always capture boolean output even if runtime writes only to DOM.
  if (env.output_bool && !env.output_bool.__forcedCapture) {
    const origBool = env.output_bool;
    env.output_bool = function(x) {
      if (origBool.__isStub !== true) {
        ioCapture.stdout += (x ? 'да' : 'нет');
      }
      return origBool(x);
    };
    env.output_bool.__forcedCapture = true;
  }
  // Wrap output_symbol to always capture character output even if runtime writes only to DOM.
  if (env.output_symbol && !env.output_symbol.__forcedCapture) {
    const origSym = env.output_symbol;
    env.output_symbol = function(x) {
      const codePoint = Number(x) >>> 0;
      const ch = String.fromCodePoint(codePoint);
      ioCapture.stdout += ch;
      return origSym(x);
    };
    env.output_symbol.__forcedCapture = true;
  }
  // Instrument print-related functions for tracing
  const traceNames = ['output_string','output_double','output_int64','output_bool','output_symbol','print','io_write','putchar'];
  for (const tName of traceNames) {
    if (env[tName] && !env[tName].__traced) {
      const orig = env[tName];
      env[tName] = function(...args) {
        if (printOutput) log('[TRACE]', tName, 'args=', args.map(a => typeof a === 'bigint' ? a.toString() : a));
        return orig.apply(this, args);
      };
      env[tName].__traced = true;
    }
  }
  const imports = { env };
  return WebAssembly.instantiate(bytes, imports).then(obj => {
    const instance = obj.instance;
    // If the module defines its own memory (no memory import), prefer that one for decoding string literals & data segment.
    if (instance.exports && instance.exports.memory && instance.exports.memory.buffer) {
      memory = instance.exports.memory; // switch to real module memory
      if (printOutput) log('[INIT] switched to module memory size pages=', memory.buffer.byteLength / 65536);
      // Re-bind memory for runtime modules that rely on the current memory reference (second chance after switch)
      if (env.__bindMemory && typeof env.__bindMemory === 'function') {
        try { env.__bindMemory(memory); if (printOutput) log('[INIT] __bindMemory re-called after memory switch'); } catch (e) { if (printOutput) log('[WARN] re-__bindMemory failed', e.message); }
      }
    }
    // Expose last used memory globally so runAll can decode pointer returns for 'лит' algorithms.
    global.__lastWasmMemory = memory;
    return { instance, memory };
  });
}

async function executeCase(wasmPath, algName, caseBase) {
  const ioCapture = { stdout: '' };
  const bytes = fs.readFileSync(wasmPath);
  // Reset DOM stdout before instantiation so only this case's output is captured.
  resetDomStdout();
  const stdinContent = loadStdinForCase(caseBase);
  const { instance } = await instantiateWasm(wasmPath, ioCapture, stdinContent);
  // Collect export function names for debugging if algorithm not found.
  const exportFnNames = Object.entries(instance.exports).filter(([n,v]) => typeof v === 'function').map(([n]) => n);
  const meta = wasmMetadata(bytes);
  if (printOutput) {
    log('[META] exports=', exportFnNames.join(','), 'startIndex=', meta.startIndex);
    if (meta.exports.length && printOutput) {
      const startEntry = meta.startIndex != null ? meta.exports.find(e => e.index === meta.startIndex) : null;
      if (startEntry) log('[META] start export name =', startEntry.name);
    }
    if (meta.imports.length) {
      for (const im of meta.imports) {
        if (im.kind === 0x00) {
          log('[META-IMPORT func]', im.modName + '.' + im.fieldName);
        } else {
          log('[META-IMPORT other]', im.modName + '.' + im.fieldName, 'kind=' + im.kind);
        }
      }
    }
  }
  // Prefer explicit algorithm export name; fallback: first export function.
  let fn = null;
  if (algName && instance.exports[algName]) fn = instance.exports[algName];
  // Additional heuristics for common "main" naming patterns if original name not exported.
  if (!fn) {
    const candidates = ['main','Main','_main','start','нач'];
    for (const c of candidates) {
      if (instance.exports[c]) { fn = instance.exports[c]; algName = c; break; }
    }
  }
  if (!fn) {
    // Prefer non-internal exported function (skip ctor/data relocation helpers)
    const internalPrefix = '__wasm_';
    const allExportFns = Object.entries(instance.exports).filter(([n,v]) => typeof v === 'function');
    const nonInternal = allExportFns.filter(([n]) => !n.startsWith(internalPrefix));
    const pick = (nonInternal.length ? nonInternal : allExportFns)[0];
    if (pick) { fn = pick[1]; algName = pick[0]; }
  }
  if (!fn && exportFnNames.length) {
    // Fallback: try invoking all zero-arg exported functions to trigger side effects.
    for (const name of exportFnNames) {
      const f = instance.exports[name];
      if (typeof f === 'function' && f.length === 0) {
        try { f(); if (printOutput) log('[MULTI-CALL]', name); } catch (e) { if (printOutput) log('[SKIP-FN]', name, e.message); }
      }
    }
    // After multi-call, attempt again to select a non-internal function if we initially only had internal.
    const internalPrefix = '__wasm_';
    const nonInternal = exportFnNames.filter(n => !n.startsWith(internalPrefix));
    algName = (nonInternal[0] || exportFnNames[0]);
    fn = instance.exports[algName];
  }
  if (!fn && exportFnNames.length) throw new Error('No executable export found for ' + wasmPath);
  const ret = fn();
  const retType = wasmReturnType(bytes, algName);
  return { returnValue: ret, stdout: ioCapture.stdout, exportName: algName, returnType: retType };
}

function normalizeReturn(ret, retType) {
  if (ret === undefined || ret === null) return '';
  // Float formatting: prefer WASM-detected types; fallback heuristic for large scientific numbers
  if (retType === 'f32' || retType === 'f64') {
    // Special-case full expansion for Number.MAX_VALUE to match C++ style golden with full integer digits.
    if (ret === Number.MAX_VALUE) {
      // Precomputed full decimal expansion of DBL_MAX (IEEE754 binary64) with fixed 15 fractional zeros.
      return '179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368.000000000000000';
    }
    return Number(ret).toFixed(15);
  }
  if (typeof ret === 'number' && isFinite(ret)) {
    const str = String(ret);
    // Heuristic: if scientific notation or extremely large magnitude, emulate C++ fixed with 15 decimals
    if (/e[+-]\d+/i.test(str) || Math.abs(ret) >= 1e21) {
      if (ret === Number.MAX_VALUE) {
        return '179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368.000000000000000';
      }
      try { return Number(ret).toFixed(15); } catch (_) { /* ignore */ }
    }
  }
  return String(ret);
}

async function runAll() {
  const compiler = findCompiler();
  const cases = collectCases(casesDir);
  let failed = 0;
  const results = []; // accumulate per-test results for optional JUnit XML
  for (const caseBase of cases) {
    // Optional test filter similar to gtest_filter: pattern can contain
    // '*' wildcards and multiple patterns separated by ':'. A leading
    // '-' section can be used to exclude patterns, e.g. "*:-io/*".
    if (filterPattern) {
      const name = caseBase.replace(/\\/g, '/');
      const parts = filterPattern.split(':');
      let included = false;
      let hasInclude = false;
      for (const p of parts) {
        if (!p) continue;
        const isNeg = p[0] === '-';
        const pat = isNeg ? p.slice(1) : p;
        if (!pat) continue;
        const re = new RegExp('^' + pat.split('*').map(s => s.replace(/[.*+?^${}()|[\\]\\]/g, '\\$&')).join('.*') + '$');
        if (!isNeg) {
          hasInclude = true;
          if (re.test(name)) included = true;
        } else {
          if (re.test(name)) { included = false; break; }
        }
      }
      if (hasInclude && !included) {
        if (printOutput) log('[SKIP-FILTER]', caseBase, 'by --filter', filterPattern);
        continue;
      }
    }
    const srcPath = path.join(casesDir, caseBase + '.kum');
    const firstLine = readAll(srcPath).split(/\r?\n/)[0];
    if (firstLine.includes('disable_exec')) {
      if (printOutput) log('[SKIP]', caseBase, '(disable_exec)');
      continue;
    }
  const wasmPath = compileCase(compiler, caseBase);
  const stdinContent = loadStdinForCase(caseBase);
  const code = readAll(srcPath);
  const { type: algType, name: algName } = parseAlgHeader(code);
  const goldenResultPath = path.join(goldensDir, caseBase + '.result');
  const goldenStdOutPath = path.join(goldensDir, caseBase + '.result.stdout');

    let exec;
    try {
      exec = await executeCase(wasmPath, algName, caseBase);
    } catch (e) {
      log('[FAIL]', caseBase, 'execution error:', e.message);
      if (printOutput) log('[DEBUG] consider adding JS runtime for imports via --runtime');
      failed++;
      continue;
    }
  let gotRet = normalizeReturn(exec.returnValue, exec.returnType);
    // Primary stdout source is ioCapture (exec.stdout). DOM (#stdout) is only a fallback
    // for runtimes that write directly to the page and don't use our stubs.
    let gotStdOut = exec.stdout || '';
    const domStdOut = getDomStdout();
    if (!gotStdOut && domStdOut) {
      gotStdOut = domStdOut;
    }

    // Read expected return early so we can use it for type-specific normalization
    let expRet = fs.existsSync(goldenResultPath) ? readAll(goldenResultPath) : null;

    // Heuristic for Кумир 'лит' (character) algorithms:
    // In C++ runtime many such algorithms (including those using str_from_unicode)
    // actually return char* pointing to a UTF-8 string, not a raw codepoint.
    // So for 'алг лит <name>' with i32 return we primarily interpret the value
    // as a pointer to a C-string in linear memory and only fall back to treating
    // it as a Unicode codepoint if decoding as a string yields nothing.
    if (algType === 'лит' && exec.returnType === 'i32') {
      const raw = Number(exec.returnValue);
      if (Number.isInteger(raw) && raw >= 0) {
        const ptr = raw >>> 0;
        let candidate = '';
        // Prefer char* semantics: decode null-terminated UTF-8 string from wasm memory.
        if (ptr !== 0 && global.__lastWasmMemory) {
          // eslint-disable-next-line no-undef
          candidate = utf8FromMemory(global.__lastWasmMemory, ptr) || '';
        }
        // Fallback: if string decoding failed or produced empty, treat as codepoint.
        if (!candidate && raw <= 0x10FFFF) {
          try {
            candidate = String.fromCodePoint(raw);
          } catch (_) {
            candidate = '';
          }
        }
        if (candidate) {
          gotRet = candidate;
        }
      }
    }

    if (update) {
      writeAll(goldenResultPath, gotRet);
      if (gotStdOut) writeAll(goldenStdOutPath, gotStdOut);
    }
    // expRet was already read above
    // Boolean formatting: if golden expects 'true'/'false' and return type is integer (i32/i64) treat non-zero as true.
    if (expRet === 'true' || expRet === 'false') {
      const isTrue = exec.returnValue !== 0 && exec.returnValue !== undefined && exec.returnValue !== null;
      gotRet = isTrue ? 'true' : 'false';
    }
    let expStdOut = fs.existsSync(goldenStdOutPath) ? readAll(goldenStdOutPath) : null;

  let ok = true;
    if (expRet === null) { log('[MISSING GOLDEN]', goldenResultPath); ok = false; }
    else if (expRet !== gotRet) { ok = false; }
    if (expStdOut !== null && expStdOut !== gotStdOut) ok = false;

    if (printOutput || !ok) {
      log('--- Case:', caseBase);
      log('Export:', exec.exportName);
      log('Return got=', gotRet, 'exp=', expRet);
      if (expStdOut !== null || gotStdOut) {
        log('StdOut got="' + gotStdOut.replace(/\n/g,'\\n') + '" exp="' + (expStdOut||'').replace(/\n/g,'\\n') + '"');
      }
    }
    log(ok ? '[OK]' : '[FAIL]', caseBase);
    results.push({
      name: caseBase,
      exportName: exec.exportName,
      ok,
      returnGot: gotRet,
      returnExp: expRet,
      stdoutGot: gotStdOut,
      stdoutExp: expStdOut,
    });
    if (!ok) failed++;
  }
  // Optional JUnit-style XML output
  if (xmlOutputPath) {
    const total = results.length;
    const failures = results.filter(r => !r.ok).length;
    const escape = (s) => String(s ?? '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&apos;');
    let xml = '';
    xml += '<?xml version="1.0" encoding="UTF-8"?>\n';
    xml += `<testsuite name="qumir-wasm" tests="${total}" failures="${failures}">` + '\n';
    for (const r of results) {
      const testName = escape(r.name);
      const className = escape(path.dirname(r.name) || '.');
      xml += `  <testcase name="${testName}" classname="${className}">` + '\n';
      if (!r.ok) {
        const msgParts = [];
        if (r.returnExp !== null) {
          msgParts.push(`Return got=${r.returnGot} exp=${r.returnExp}`);
        } else {
          msgParts.push('Missing golden .result');
        }
        if (r.stdoutExp !== null || r.stdoutGot) {
          msgParts.push(`StdOut got="${(r.stdoutGot || '').replace(/\n/g, '\\n')}" exp="${(r.stdoutExp || '').replace(/\n/g, '\\n')}"`);
        }
        const message = msgParts.join('; ');
        xml += `    <failure message="${escape(message)}"/>` + '\n';
      }
      // Optionally include stdout as system-out even for passing tests
      if (r.stdoutGot) {
        xml += '    <system-out>' + escape(r.stdoutGot) + '</system-out>\n';
      }
      xml += '  </testcase>\n';
    }
    xml += '</testsuite>\n';
    fs.mkdirSync(path.dirname(xmlOutputPath), { recursive: true });
    fs.writeFileSync(xmlOutputPath, xml, 'utf8');
    if (printOutput) log('[XML] wrote JUnit report to', xmlOutputPath);
  }
  if (failed) {
    log('Total failed:', failed);
    process.exit(1);
  } else {
    log('All wasm execution tests passed.');
  }
}

runAll().catch(e => { log('Fatal error:', e); process.exit(1); });
