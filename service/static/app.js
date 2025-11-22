import { bindBrowserIO } from './io_wrapper.js';

'use strict';

const $ = sel => document.querySelector(sel);
let currentAbort = null;
// Output view selection for the compiler pane (text or turtle)
let __compilerOutputMode = 'text';
let __turtleCanvas = null;
let __turtleToggle = null;
let __turtleModule = null;
let __ioBound = false;
const api = async (path, body, asBinary, signal) => {
  // New protocol: send raw code as text/plain and pass optimization level via X-Qumir-O
  const code = body.code || '';
  const O = body.O || '0';
  const r = await fetch(path, { method: 'POST', headers: { 'Content-Type': 'text/plain', 'X-Qumir-O': String(O) }, body: code, signal });
  if (!r.ok) {
    let msg;
    try { const j = await r.json(); msg = j.error || r.statusText; } catch { msg = r.statusText; }
    throw new Error(msg);
  }
  return asBinary ? new Uint8Array(await r.arrayBuffer()) : await r.text();
};

// Simple GET helper for text/json
const apiGet = async (path) => {
  const r = await fetch(path, { method: 'GET' });
  if (!r.ok) throw new Error(await r.text());
  const ct = r.headers.get('Content-Type') || '';
  if (ct.includes('application/json')) return await r.json();
  return await r.text();
};

const sample = `алг цел цикл\nнач\n    | пример комментария: горячий цикл для теста производительности\n    цел ф, i\n    нц для i от 1 до 10000000\n        ф := факториал(13)\n    кц\n    знач := ф\nкон\n\nалг цел факториал(цел число)\nнач\n    | пример комментария внутри функции\n    цел i\n    знач := 1\n    нц для i от 1 до число\n        знач := знач * i\n    кц\nкон\n`;

function parseAlgHeader(code) {
  const lines = code.split(/\r?\n/);
  for (const line of lines) {
    if (!/^\s*алг\s+/u.test(line)) continue;
    const trimmed = line.trim();
    const tokens = trimmed.split(/\s+/);
    if (tokens.length === 2) {
      return { type: null, name: tokens[1].replace(/\(.*/, '') };
    }
    if (tokens.length >= 3) {
      return { type: tokens[1], name: tokens[2].replace(/\(.*/, '') };
    }
  }
  return { type: null, name: null };
}

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
// Helpers: turtle UI in the compiler output pane
function ensureTurtleUI() {
  const out = document.getElementById('output');
  if (!out) return;
  // Toggle UI (radio buttons)
  if (!__turtleToggle) {
    const ctr = document.createElement('div');
    ctr.id = 'output-mode';
    ctr.className = 'output-mode';
    ctr.style.margin = '6px 0';
    ctr.style.display = 'flex';
    ctr.style.gap = '12px';
    const makeOpt = (label, value) => {
      const lab = document.createElement('label');
      lab.style.cursor = 'pointer';
      const input = document.createElement('input');
      input.type = 'radio';
      input.name = 'q-out-mode';
      input.value = value;
      input.style.marginRight = '6px';
      input.addEventListener('change', () => { if (input.checked) setCompilerOutputMode(value); });
      lab.appendChild(input);
      lab.appendChild(document.createTextNode(label));
      return lab;
    };
    ctr.appendChild(makeOpt('Текст', 'text'));
    ctr.appendChild(makeOpt('Черепаха', 'turtle'));
    out.parentNode.insertBefore(ctr, out);
    __turtleToggle = ctr;
  }
  __turtleToggle.style.display = '';
  // Sync radios with current mode or saved cookie
  const saved = getCookie('q_out_mode');
  const targetMode = (saved === 'turtle') ? 'turtle' : __compilerOutputMode;
  const radios = __turtleToggle.querySelectorAll('input[name="q-out-mode"]');
  radios.forEach(r => { r.checked = (r.value === targetMode); });

  if (!__turtleCanvas) {
    const cnv = document.createElement('canvas');
    cnv.id = 'turtle-canvas';
    cnv.style.display = 'none';
    cnv.style.width = '100%';
    // try to mirror output height if fixed; otherwise default
    cnv.style.height = (out.clientHeight > 0 ? out.clientHeight + 'px' : (out.style.height || '300px'));
    cnv.style.background = '#fff';
    cnv.style.border = '1px solid #2b2b2b44';
    cnv.style.borderRadius = '4px';
    out.parentNode.insertBefore(cnv, out.nextSibling);
    __turtleCanvas = cnv;
    if (window.ResizeObserver) {
      const ro = new ResizeObserver(() => {
        if (__turtleCanvas && out) {
          const h = out.clientHeight;
          if (h > 32) __turtleCanvas.style.height = h + 'px';
        }
      });
      try { ro.observe(out); } catch {}
    }
    // One-time async adjust in case layout not settled yet
    setTimeout(() => {
      if (__turtleCanvas && out) {
        const h = out.clientHeight;
        if (h > 32) __turtleCanvas.style.height = h + 'px';
      }
    }, 0);
  } else {
    // Update existing canvas height if output grew before first toggle
    if (out.clientHeight > 32) __turtleCanvas.style.height = out.clientHeight + 'px';
  }
}

function hideTurtleUI() {
  __compilerOutputMode = 'text';
  setCookie('q_out_mode', 'text');
  if (__turtleToggle) __turtleToggle.style.display = 'none';
  const out = document.getElementById('output');
  if (out) out.style.display = '';
  if (__turtleCanvas) __turtleCanvas.style.display = 'none';
}

function setCompilerOutputMode(mode) {
  __compilerOutputMode = (mode === 'turtle') ? 'turtle' : 'text';
  setCookie('q_out_mode', __compilerOutputMode);
  const out = document.getElementById('output');
  if (__compilerOutputMode === 'turtle') {
    if (out) out.style.display = 'none';
    if (__turtleCanvas) __turtleCanvas.style.display = '';
    // Sync height again to avoid initial gap
    if (__turtleCanvas && out && out.clientHeight > 32) {
      __turtleCanvas.style.height = out.clientHeight + 'px';
    }
    // Inform turtle runtime that canvas is now visible so it can refit using real dimensions
    try { if (__turtleModule && typeof __turtleModule.__onCanvasShown === 'function') __turtleModule.__onCanvasShown(); } catch {}
  } else {
    if (out) out.style.display = '';
    if (__turtleCanvas) __turtleCanvas.style.display = 'none';
  }
  if (__turtleToggle) {
    const radios = __turtleToggle.querySelectorAll('input[name="q-out-mode"]');
    radios.forEach(r => { r.checked = (r.value === __compilerOutputMode); });
  }
}

function getCurrentCompilerOutputNode() {
  return (__compilerOutputMode === 'turtle' && __turtleCanvas) ? __turtleCanvas : document.getElementById('output');
}

async function runWasm() {
  const code = getCode();
  const { type: algType } = parseAlgHeader(code);
  const O = $('#opt').value;
  try {
    const bytes = await api('/api/compile-wasm', { code, O }, true);
    const mathEnv = await import('./runtime/math.js');
    const ioEnv = await import('./runtime/io.js');
    const resultEnv = await import('./runtime/result.js');
    if (!__ioBound) {
      bindBrowserIO(ioEnv);
      __ioBound = true;
    }
    const stringEnv = await import('./runtime/string.js');
    const arrayEnv = await import('./runtime/array.js');
    if (!__turtleModule) { try { __turtleModule = await import('./runtime/turtle.js'); } catch {} }
    const env = { ...mathEnv, ...ioEnv, ...stringEnv, ...arrayEnv, ...(__turtleModule || {}) };
    const imports = { env };
    const { instance, module } = await WebAssembly.instantiate(bytes, imports);
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
    // Turtle integration: detect if wasm imports turtle_* and prepare canvas/toggle
    let usesTurtle = false;
    try {
      const imps = module ? WebAssembly.Module.imports(module) : [];
      usesTurtle = Array.isArray(imps) && imps.some(imp => imp && imp.module === 'env' && typeof imp.name === 'string' && imp.name.startsWith('turtle_'));
    } catch {}
    if (usesTurtle && __turtleModule) {
      ensureTurtleUI();
      if (__turtleCanvas && typeof __turtleModule.__bindTurtleCanvas === 'function') {
        __turtleModule.__bindTurtleCanvas(__turtleCanvas);
      }
      if (typeof __turtleModule.__resetTurtle === 'function') {
        __turtleModule.__resetTurtle(true);
      }
      const saved = getCookie('q_out_mode');
      setCompilerOutputMode(saved === 'turtle' ? 'turtle' : __compilerOutputMode);
    } else {
      hideTurtleUI();
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
          const retType = resultEnv.wasmReturnType(bytes, name);
          const normalized = resultEnv.normalizeReturnValue(res, {
            returnType: retType,
            algType,
            memory: mem
          });
          out += `${name} => ${normalized}\n`;
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
  // Responsive height: fixed on desktop, auto on mobile (CSS controls heights)
  const __applyEditorHeight = () => {
    if (window.innerWidth <= 900) {
      editor.setSize(null, 'auto');
    } else {
      editor.setSize(null, 420);
    }
    editor.refresh();
  };
  __applyEditorHeight();
  window.addEventListener('resize', __applyEditorHeight);
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
// Load examples list
(async function initExamples(){
  try {
    const data = await apiGet('/api/examples');
    const sel = $('#examples');
    if (sel && data && Array.isArray(data.examples)) {
      // Fill options grouped by folder prefix
      // Build a simple flat list: "folder/file.kum"
      data.examples.forEach(it => {
        const opt = document.createElement('option');
        opt.value = it.path;
        opt.textContent = it.path;
        sel.appendChild(opt);
      });
    }
  } catch (e) {
    console.warn('examples load failed:', e);
  }
})();
// Initialize editor (assets are loaded via HTML)
initEditor();

// Relocate the compiler view selector above the Output on mobile
(function relocateViewSelector(){
  const viewEl = document.getElementById('view');
  if (!viewEl) return;
  // Create an anchor to restore original position in header controls
  let anchor = document.getElementById('view-anchor');
  if (!anchor) {
    anchor = document.createElement('span');
    anchor.id = 'view-anchor';
    viewEl.insertAdjacentElement('afterend', anchor);
  }
  const slot = document.getElementById('view-slot');
  const place = () => {
    if (!viewEl) return;
    if (window.innerWidth <= 900 && slot && viewEl.parentElement !== slot) {
      slot.appendChild(viewEl);
    } else if (window.innerWidth > 900 && anchor && viewEl.previousSibling !== anchor) {
      // Put it back before the anchor to keep control layout
      anchor.parentNode.insertBefore(viewEl, anchor);
    }
  };
  place();
  window.addEventListener('resize', place);
})();

// On mobile, place compiler output below IO section to ensure strict vertical flow
(function relocateOutputMobile(){
  const rightPane = document.querySelector('section.pane.right');
  const io = document.querySelector('section.io');
  const anchor = document.getElementById('output-anchor');
  if (!rightPane || !io || !anchor) return;
  const place = () => {
    if (window.innerWidth <= 900) {
      // Move right pane after IO
      if (rightPane.previousElementSibling !== io) {
        io.insertAdjacentElement('afterend', rightPane);
      }
    } else {
      // Restore to anchor position inside main
      if (anchor.parentNode && rightPane !== anchor.nextSibling) {
        anchor.parentNode.insertBefore(rightPane, anchor.nextSibling);
      }
    }
  };
  place();
  window.addEventListener('resize', place);
})();
// If URL has ?share=<id>, load the shared snippet (code, args, stdin) and override state
(async function loadSharedFromQuery(){
  try {
    const params = new URLSearchParams(window.location.search);
    const sid = params.get('share');
    if (!sid) return;
    const data = await apiGet('/api/share?id=' + encodeURIComponent(sid));
    if (typeof data === 'string') {
      setCode(data);
    } else if (data && typeof data === 'object') {
      if (typeof data.code === 'string') setCode(data.code);
      if (typeof data.args === 'string' && $('#args')) $('#args').value = data.args;
      if (typeof data.stdin === 'string' && $('#stdin')) $('#stdin').value = data.stdin;
    }
    saveState();
    debounceShow();
    const statusEl = document.getElementById('status');
    if (statusEl) statusEl.textContent = `Загружено из ссылки: ${sid}`;
  } catch (e) {
    console.warn('failed to load share:', e);
  }
})();

// Populate version from backend git
(async function showVersion(){
  try {
    const v = await apiGet('/api/version');
    const el = document.getElementById('version');
    if (el) {
      if (typeof v === 'string') {
        el.textContent = 'v ' + v;
      } else if (v && v.hash && v.date) {
        el.textContent = `v ${v.hash} • ${v.date}`;
      }
    }
  } catch (e) {
    // ignore if endpoint not available
  }
})();
['#args', '#stdin'].forEach(sel => {
  const el = $(sel);
  if (el) el.addEventListener('input', saveState);
});
const viewSel = $('#view');
if (viewSel) viewSel.addEventListener('change', () => { saveState(); show(viewSel.value); });
const optSel = $('#opt');
if (optSel) optSel.addEventListener('change', () => { saveState(); show($('#view').value); });

// Auto-load example when selection changes
const examplesSel = $('#examples');
if (examplesSel) examplesSel.addEventListener('change', async () => {
  const path = examplesSel.value || '';
  if (!path) return;
  try {
    const txt = await apiGet('/api/example?path=' + encodeURIComponent(path));
    setCode(txt);
    saveState();
    debounceShow();
  } catch (e) {
    alert('Не удалось загрузить пример: ' + (e.message || String(e)));
  }
});

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

// Fullscreen viewer for outputs (stdout/output)
(function setupFullscreenViewer(){
  const overlay = document.getElementById('fs-overlay');
  const body = document.getElementById('fs-body');
  const title = document.getElementById('fs-title');
  const closeBtn = document.getElementById('fs-close');
  if (!overlay || !body || !title || !closeBtn) return;
  let restore = null;
  const open = (label, nodeOrText) => {
    title.textContent = label || 'Output';
    body.innerHTML = '';
    overlay.classList.add('show');
    overlay.setAttribute('aria-hidden', 'false');
    // If node passed, move it; otherwise render text in <pre>
    if (nodeOrText && (nodeOrText.nodeType === 1)) {
      const node = nodeOrText;
      const parent = node.parentNode;
      const next = node.nextSibling;
      body.appendChild(node);
      // Special handling for CodeMirror: refresh after move
      try { if (editor) { editor.refresh(); } } catch {}
      restore = () => { if (parent) parent.insertBefore(node, next); try { if (editor) { editor.refresh(); } } catch {} };
    } else {
      const pre = document.createElement('pre');
      pre.textContent = nodeOrText || '';
      body.appendChild(pre);
      restore = null;
    }
  };
  const close = () => {
    overlay.classList.remove('show');
    overlay.setAttribute('aria-hidden', 'true');
    if (restore) { try { restore(); } finally { restore = null; } }
  };
  closeBtn.addEventListener('click', close);
  overlay.addEventListener('click', (e) => { if (e.target === overlay) close(); });
  window.addEventListener('keydown', (e) => { if (e.key === 'Escape') close(); });
  const stdoutEl = document.getElementById('stdout');
  const titleStdout = document.getElementById('title-stdout');
  if (stdoutEl && titleStdout) {
    titleStdout.addEventListener('mousedown', e => e.preventDefault());
    titleStdout.addEventListener('click', () => open('Program output (stdout)', stdoutEl));
  }
  const compOutEl = document.getElementById('output');
  const titleOutput = document.getElementById('title-output');
  if (compOutEl && titleOutput) {
    titleOutput.addEventListener('mousedown', e => e.preventDefault());
    titleOutput.addEventListener('click', () => {
      const node = getCurrentCompilerOutputNode();
      open('Compiler output', node);
    });
  }
  const stdinEl = document.getElementById('stdin');
  const titleStdin = document.getElementById('title-stdin');
  if (stdinEl && titleStdin) {
    titleStdin.addEventListener('mousedown', e => e.preventDefault());
    titleStdin.addEventListener('click', () => open('Program input (stdin)', stdinEl));
  }
  const titleCode = document.getElementById('title-code');
  if (titleCode) {
    titleCode.addEventListener('mousedown', e => e.preventDefault());
    titleCode.addEventListener('click', () => {
      // Move the CodeMirror wrapper if exists, otherwise textarea
      const cm = editor && editor.getWrapperElement ? editor.getWrapperElement() : null;
      if (cm) open('Code', cm); else open('Code', document.getElementById('code'));
    });
  }
})();

// Toast helper
let __toastEl = null;
let __toastTimer = null;
function showToast(message, ms = 2000) {
  if (!__toastEl) {
    __toastEl = document.createElement('div');
    __toastEl.className = 'q-toast';
    document.body.appendChild(__toastEl);
  }
  __toastEl.textContent = message;
  __toastEl.classList.add('show');
  if (__toastTimer) clearTimeout(__toastTimer);
  __toastTimer = setTimeout(() => {
    __toastEl.classList.remove('show');
  }, Math.max(500, ms|0));
}

// Share: POST current code to /api/share and copy link
const btnShare = document.getElementById('btn-share');
if (btnShare) {
  btnShare.addEventListener('click', async () => {
    const code = getCode();
  const args = $('#args') ? $('#args').value : '';
  const stdin = $('#stdin') ? $('#stdin').value : '';
    try {
      const r = await fetch('/api/share', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ code, args, stdin })
      });
      if (!r.ok) throw new Error(await r.text());
      const res = await r.json();
      const url = res && res.url ? res.url : (res.raw_url || '');
      if (url) {
        try { await navigator.clipboard.writeText(url); } catch {}
        // Update location without reload
        try {
          if (res.id) {
            const pretty = `/s/${encodeURIComponent(res.id)}`;
            window.history.replaceState({}, '', pretty);
          }
        } catch {}
        showToast('Ссылка скопирована в буфер обмена', 2000);
      }
    } catch (e) {
      alert('Не удалось создать ссылку: ' + (e.message || String(e)));
    }
  });
}
