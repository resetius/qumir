'use strict';

const $ = sel => document.querySelector(sel);
let currentAbort = null;
const api = async (path, body, asBinary, signal) => {
  const r = await fetch(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body), signal });
  if (!r.ok) {
    let msg;
    try { const j = await r.json(); msg = j.error || r.statusText; } catch { msg = r.statusText; }
    throw new Error(msg);
  }
  return asBinary ? new Uint8Array(await r.arrayBuffer()) : await r.text();
};

const sample = `алг\nнач\n    цел i\n    i := 1\n    вывод i, нс\nкон\n`;

function setCookie(name, value, days = 365) {
  const expires = `max-age=${days*24*60*60}`;
  document.cookie = `${encodeURIComponent(name)}=${encodeURIComponent(value)}; ${expires}; path=/`;
}

function getCookie(name) {
  const n = encodeURIComponent(name) + '=';
  const parts = document.cookie.split(';');
  for (let p of parts) {
    p = p.trim();
    if (p.startsWith(n)) return decodeURIComponent(p.substring(n.length));
  }
  return null;
}

function hexdump(bytes) {
  let out = '';
  for (let i = 0; i < bytes.length; i += 16) {
    const chunk = bytes.slice(i, i + 16);
    const hex = Array.from(chunk).map(b => b.toString(16).padStart(2, '0')).join(' ');
    const ascii = Array.from(chunk).map(b => (b >= 32 && b < 127) ? String.fromCharCode(b) : '.').join('');
    out += i.toString(16).padStart(8, '0') + '  ' + hex.padEnd(16*3-1, ' ') + '  ' + ascii + '\n';
  }
  return out;
}

async function show(mode) {
  const code = $('#code').value;
  const O = $('#opt').value;
  const map = {
    ir: ['/api/compile-ir', false],
    llvm: ['/api/compile-llvm', false],
    asm: ['/api/compile-asm', false],
    wasm: ['/api/compile-wasm-text', false],
  };
  const [endpoint, bin] = map[mode] || map.ir;
  if (currentAbort) currentAbort.abort();
  currentAbort = new AbortController();
  const { signal } = currentAbort;
  try {
    $('#output').classList.remove('error');
    const data = await api(endpoint, { code, O }, bin, signal);
    if (bin) {
      $('#output').textContent = hexdump(data);
    } else {
      $('#output').textContent = data;
    }
  } catch (e) {
    if (e.name === 'AbortError') return;
    $('#output').textContent = e.message;
    $('#output').classList.add('error');
  }
}

async function runWasm() {
  const code = $('#code').value;
  const O = $('#opt').value;
  try {
    const bytes = await api('/api/compile-wasm', { code, O }, true);
    const mathEnv = await import('./runtime/math.js');
    const ioEnv = await import('./runtime/io.js');
    const env = { ...mathEnv, ...ioEnv };
    const imports = { env };
    const { instance } = await WebAssembly.instantiate(bytes, imports);
    if (instance.exports && instance.exports.memory && typeof ioEnv.__bindMemory === 'function') {
      ioEnv.__bindMemory(instance.exports.memory);
    }
    if (typeof ioEnv.__resetIO === 'function') {
      ioEnv.__resetIO(true);
    }
    let out = '';
    if (instance && instance.exports) {
      const entries = Object.entries(instance.exports)
        .filter(([k, v]) => typeof v === 'function' && !k.startsWith('__'));
      const entry = entries.length > 0 ? entries[0] : null;
      if (entry) {
        const [name, fn] = entry;
        const rawArgs = ($('#args').value || '').trim();
        const argv = rawArgs.length ? rawArgs.split(',').map(s => s.trim()) : [];
        const parsed = argv.map(s => {
          if (s === 'истина' || s.toLowerCase() === 'true') return 1;
          if (s === 'ложь' || s.toLowerCase() === 'false') return 0;
          if (/^[-+]?\d+$/.test(s)) return BigInt(s);
          if (/^[-+]?\d*\.\d+(e[-+]?\d+)?$/i.test(s) || /^[-+]?\d+\.\d*(e[-+]?\d+)?$/i.test(s)) return Number(s);
          if ((s.startsWith('"') && s.endsWith('"')) || (s.startsWith("'") && s.endsWith("'"))) return s.slice(1, -1);
          return s;
        });
        const expected = typeof fn.length === 'number' ? fn.length : undefined;
        if (expected !== undefined && parsed.length !== expected) {
          out += `${name} expects ${expected} arg(s), got ${parsed.length}.\n`;
        } else {
          const t0 = (typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now();
          const res = fn(...parsed);
          const t1 = (typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now();
          const micros = Math.round((t1 - t0) * 1000);
          out += `${name} => ${String(res)}\n`;
          out += `time: ${micros} µs\n`;
        }
      } else {
        out += 'no exported functions to invoke\n';
      }
      out += '\nexports:\n';
      for (const [k, v] of Object.entries(instance.exports)) {
        out += ` - ${k}: ${typeof v}\n`;
      }
    }
    $('#stdout').textContent = out;
  } catch (e) {
    $('#stdout').textContent = e.message;
    $('#stdout').classList.add('error');
  }
}
function loadState() {
  const c = getCookie('q_code');
  $('#code').value = (c !== null && c !== undefined) ? c : sample;
  const a = getCookie('q_args');
  if (a !== null && a !== undefined) $('#args').value = a;
  const i = getCookie('q_stdin');
  if (i !== null && i !== undefined) $('#stdin').value = i;
  const v = getCookie('q_view');
  if (v !== null && v !== undefined) $('#view').value = v;
  const o = getCookie('q_opt');
  if (o !== null && o !== undefined) $('#opt').value = o;
}

function saveState() {
  setCookie('q_code', $('#code').value);
  setCookie('q_args', $('#args').value || '');
  setCookie('q_stdin', $('#stdin').value || '');
  setCookie('q_view', $('#view').value || 'ir');
  setCookie('q_opt', $('#opt').value || '0');
}

loadState();
['#code', '#args', '#stdin'].forEach(sel => {
  const el = $(sel);
  if (el) el.addEventListener('input', saveState);
});
const viewSel = $('#view');
if (viewSel) viewSel.addEventListener('change', () => { saveState(); show(viewSel.value); });
const optSel = $('#opt');
if (optSel) optSel.addEventListener('change', () => { saveState(); show($('#view').value); });

// Debounce auto-show on code edits to avoid spamming service
let showTimer = null;
const debounceShow = () => {
  if (showTimer) clearTimeout(showTimer);
  showTimer = setTimeout(() => show($('#view').value), 350);
};
const codeEl = $('#code');
if (codeEl) codeEl.addEventListener('input', debounceShow);

// Auto show on first load
show($('#view').value);

// Ensure Run also refreshes the right pane
$('#btn-run').addEventListener('click', async () => {
  await runWasm();
  show($('#view').value);
});
