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

const sample = `алг цел цикл\nнач\n    | пример комментария: горячий цикл для теста производительности\n    цел ф, i\n    нц для i от 1 до 10000000\n        ф := факториал(13)\n    кц\n    знач := ф\nкон\n\nалг цел факториал(цел число)\nнач\n    | пример комментария внутри функции\n    цел i\n    знач := 1\n    нц для i от 1 до число\n        знач := знач * i\n    кц\nкон\n`;

// CodeMirror editor (initialized below if library present)
let editor = null;
function getCode() {
  if (editor) return editor.getValue();
  const el = document.getElementById('code');
  return el ? el.value : '';
}
function setCode(text) {
  if (editor) return editor.setValue(text);
  const el = document.getElementById('code');
  if (el) el.value = text;
}

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
  const code = getCode();
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
  const code = getCode();
  const O = $('#opt').value;
  try {
    const bytes = await api('/api/compile-wasm', { code, O }, true);
    const mathEnv = await import('./runtime/math.js');
    const ioEnv = await import('./runtime/io.js');
    const stringEnv = await import('./runtime/string.js');
    const arrayEnv = await import('./runtime/array.js');
    const env = { ...mathEnv, ...ioEnv, ...stringEnv, ...arrayEnv };
    const imports = { env };
    const { instance } = await WebAssembly.instantiate(bytes, imports);
    const mem = instance.exports && instance.exports.memory;
    if (mem && typeof ioEnv.__bindMemory === 'function') {
      ioEnv.__bindMemory(mem);
    }
    if (mem && typeof stringEnv.__bindMemory === 'function') {
      stringEnv.__bindMemory(mem);
    }
    if (mem && typeof arrayEnv.__bindMemory === 'function') {
      arrayEnv.__bindMemory(mem);
    }
    if (typeof ioEnv.__resetIO === 'function') {
      ioEnv.__resetIO(true);
    }
    if (typeof stringEnv.__resetStrings === 'function') {
      stringEnv.__resetStrings();
    }
    if (typeof arrayEnv.__resetArrays === 'function') {
      arrayEnv.__resetArrays();
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
  // Debug: list of WebAssembly exports (disabled)
  // out += '\nexports:\n';
  // for (const [k, v] of Object.entries(instance.exports)) {
  //   out += ` - ${k}: ${typeof v}\n`;
  // }
    }
    const stdoutEl = $('#stdout');
    stdoutEl.textContent += "\n";
    stdoutEl.textContent += out;
  } catch (e) {
    $('#stdout').textContent = e.message;
    $('#stdout').classList.add('error');
  }
}
function loadState() {
  const c = getCookie('q_code');
  setCode((c !== null && c !== undefined) ? c : sample);
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
  setCookie('q_code', getCode());
  setCookie('q_args', $('#args').value || '');
  setCookie('q_stdin', $('#stdin').value || '');
  setCookie('q_view', $('#view').value || 'ir');
  setCookie('q_opt', $('#opt').value || '0');
}

// Initialize CodeMirror if available
function initEditor() {
  const ta = document.getElementById('code');
  if (!ta || typeof window.CodeMirror === 'undefined') return;
  // Define a simple mode for Qumir language (Cyrillic keywords)
  if (window.CodeMirror.simpleMode && !window.CodeMirror.modes['qumir']) {
    window.CodeMirror.defineSimpleMode('qumir', {
      start: [
        { regex: /\s*(\|.*$)/, token: 'comment' },
        { regex: /(алг|нач|кон|если|иначе|все|нц|кц|пока|для|шаг|вывод|ввод|цел|вещ|лог|стр)/u, token: 'keyword' },
        { regex: /(истина|ложь)/u, token: 'atom' },
        { regex: /[-+]?\d+(?:_\d+)*(?:[eE][-+]?\d+)?/, token: 'number' },
        { regex: /[-+]?\d*\.\d+(?:[eE][-+]?\d+)?/, token: 'number' },
        { regex: /"(?:[^"\\]|\\.)*"/, token: 'string' },
        { regex: /'(?:[^'\\]|\\.)*'/, token: 'string' },
        { regex: /(\+|\-|\*|\/|%|==|!=|<=|>=|<|>|:=|=|,)/, token: 'operator' },
        { regex: /[A-Za-zА-Яа-я_][A-Za-zА-Яа-я_0-9]*/u, token: 'variable' },
      ],
      meta: { lineComment: '|' }
    });
  }
  // Preserve current textarea content
  const initialText = ta.value;
  editor = window.CodeMirror.fromTextArea(ta, {
    lineNumbers: true,
    tabSize: 4,
    indentUnit: 4,
    indentWithTabs: true,
    matchBrackets: true,
    theme: 'material-darker',
    mode: 'qumir',
    extraKeys: {
      Tab: cm => cm.execCommand('indentMore'),
      'Shift-Tab': cm => cm.execCommand('indentLess'),
      'Ctrl-/': cm => cm.execCommand('toggleComment')
    }
  });
  editor.setSize(null, 420);
  // Set initial text explicitly (getCode would query editor and return empty on first init)
  editor.setValue(initialText);
  // Cursor status line
  const status = document.getElementById('status');
  if (status) {
    editor.on('cursorActivity', () => {
      const p = editor.getCursor();
      status.textContent = `Ln ${p.line + 1}, Col ${p.ch + 1}`;
    });
  }
  // Mirror initial text and change events
  editor.on('change', () => { saveState(); debounceShow(); });
  // Ensure layout after attach
  setTimeout(() => editor.refresh(), 0);
}

loadState();
// Initialize editor (assets are loaded via HTML)
initEditor();
['#args', '#stdin'].forEach(sel => {
  const el = $(sel);
  if (el) el.addEventListener('input', saveState);
});
const viewSel = $('#view');
if (viewSel) viewSel.addEventListener('change', () => { saveState(); show(viewSel.value); });
const optSel = $('#opt');
if (optSel) optSel.addEventListener('change', () => { saveState(); show($('#view').value); });

// Snippet insertion
function insertSnippet(kind) {
  const indent = '    ';
  const snippets = {
    while:
`нц пока условие
${indent}| тело
кц`,
    for:
`нц для i от 0 до 10 шаг 1
${indent}| тело
кц`,
    if:
`если условие то
${indent}| then
иначе
${indent}| else
все`,
    switch:
`выбор выражение
${indent}при 1:
${indent}${indent}| ветка 1
${indent}при 2:
${indent}${indent}| ветка 2
${indent}иначе:
${indent}${indent}| иначе
все`,
    func:
`алг цел имя(цел a)
нач
${indent}знач := a
кон`,
    decl:
`цел x, y
| табличный тип: цел таб[0..9]`
  };
  const text = snippets[kind] || '';
  if (!text) return;
  if (editor) {
    const doc = editor.getDoc();
    const cur = doc.getCursor();
    doc.replaceRange(text, cur);
    editor.focus();
  } else {
    const ta = document.getElementById('code');
    if (!ta) return;
    const start = ta.selectionStart || 0;
    const end = ta.selectionEnd || start;
    const before = ta.value.slice(0, start);
    const after = ta.value.slice(end);
    ta.value = before + text + after;
    ta.selectionStart = ta.selectionEnd = start + text.length;
    ta.focus();
  }
  saveState();
  debounceShow();
}

['while','for','if','switch','func','decl'].forEach(k => {
  const btn = document.getElementById(`btn-snippet-${k}`);
  if (btn) {
    btn.addEventListener('click', () => insertSnippet(k));
  }
});

// Build rich tooltips using snippet content (set immediately)
{ const indent = '    ';
  const preview = {
    while:
`Вставить: цикл пока\n\nнц пока условие\n${indent}| тело\nкц`,
    for:
`Вставить: цикл от\n\nнц для i от 0 до 10 шаг 1\n${indent}| тело\nкц`,
    if:
`Вставить: условие\n\nесли условие то\n${indent}| then\nиначе\n${indent}| else\nвсе`,
    switch:
`Вставить: выбор\n\nвыбор выражение\n${indent}при 1:\n${indent}${indent}| ветка 1\n${indent}при 2:\n${indent}${indent}| ветка 2\n${indent}иначе:\n${indent}${indent}| иначе\nвсе`,
    func:
`Вставить: функция\n\nалг цел имя(цел a)\nнач\n${indent}знач := a\nкон`,
    decl:
`Вставить: тип/объявление\n\nцел x, y\n| табличный тип: цел таб[0..9]`
  };
  ['while','for','if','switch','func','decl'].forEach(k => {
    const btn = document.getElementById(`btn-snippet-${k}`);
    if (btn) btn.setAttribute('data-tooltip', preview[k]);
  });
}

// JS-driven tooltip (more reliable across browsers)
(() => {
  let tipEl = null;
  function showTip(target) {
    const msg = target.getAttribute('data-tooltip');
    if (!msg) return;
    if (!tipEl) {
      tipEl = document.createElement('div');
      tipEl.className = 'q-tooltip';
      document.body.appendChild(tipEl);
    }
    tipEl.textContent = msg;
    tipEl.style.display = 'block';
    const r = target.getBoundingClientRect();
    const pad = 8;
    const top = r.bottom + pad;
    const left = Math.max(8, Math.min(window.innerWidth - tipEl.offsetWidth - 8, r.left + r.width / 2 - (tipEl.offsetWidth / 2)));
    tipEl.style.top = `${top}px`;
    tipEl.style.left = `${left}px`;
  }
  function hideTip() {
    if (tipEl) tipEl.style.display = 'none';
  }
  ['while','for','if','switch','func','decl'].forEach(k => {
    const btn = document.getElementById(`btn-snippet-${k}`);
    if (!btn) return;
    btn.addEventListener('mouseenter', () => showTip(btn));
    btn.addEventListener('mouseleave', hideTip);
    btn.addEventListener('focus', () => showTip(btn));
    btn.addEventListener('blur', hideTip);
  });
})();

// Debounce auto-show on code edits to avoid spamming service
let showTimer = null;
const debounceShow = () => {
  if (showTimer) clearTimeout(showTimer);
  showTimer = setTimeout(() => show($('#view').value), 350);
};
// Textarea fallback listener is not needed when CodeMirror is used

// Auto show on first load
show($('#view').value);

// Ensure Run also refreshes the right pane
$('#btn-run').addEventListener('click', async () => {
  await runWasm();
  show($('#view').value);
});
