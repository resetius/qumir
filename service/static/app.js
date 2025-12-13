import { bindBrowserIO } from './io_wrapper.js';
import * as resultEnv from './runtime/result.js';
import * as stringEnv from './runtime/string.js';
import { initDocs } from './docs.js';

'use strict';

const $ = sel => document.querySelector(sel);
let currentAbort = null;
// Output view selection for the compiler pane (text or turtle)
let __compilerOutputMode = 'text';
let __turtleCanvas = null;
let __turtleToggle = null;
let __turtleModule = null;
let __robotModule = null;
let __robotCanvas = null;
let __ioBound = false;
const IO_PANE_COOKIE = 'q_io_pane';
let __ioFiles = [];
let __ioSelectEl = null;
let __ioFilesRoot = null;
let __currentIoPane = 'stdout';
let __ioFileCounter = 0;
let __browserFileManager = null;
const PROJECTS_STORAGE_KEY = 'q_projects';
const ACTIVE_PROJECT_KEY = 'q_active_project';
let __projects = [];
let __activeProjectId = null;
let __projectsDrawer = null;
let __projectsListEl = null;
let __projectsBackdrop = null;
let __projectToggleBtn = null;
let __projectActiveNameEl = null;
let __projectNewBtn = null;
let __projectsRenderPending = false;
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

const sample = `алг цел цикл\nнач\n    | пример комментария: горячий цикл для теста производительности\n    цел ф, i\n    ф := 1\n    нц для i от 1 до 10000000\n        ф := факториал(13)\n    кц\n    знач := ф\nкон\n\nалг цел факториал(цел число)\nнач\n    | пример комментария внутри функции\n    цел i\n    знач := 1\n    нц для i от 1 до число\n        знач := знач * i\n    кц\nкон\n`;

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

function readPersistedValue(name) {
  try {
    if (typeof window !== 'undefined' && window.localStorage) {
      const stored = window.localStorage.getItem(name);
      if (stored !== null && stored !== undefined) return stored;
    }
  } catch (err) {
    console.warn('localStorage read failed:', err);
  }
  return getCookie(name);
}

function writePersistedValue(name, value) {
  const payload = value ?? '';
  try {
    if (typeof window !== 'undefined' && window.localStorage) {
      window.localStorage.setItem(name, payload);
    }
  } catch (err) {
    console.warn('localStorage write failed:', err);
  }
  setCookie(name, payload);
}

function generateProjectId() {
  return `project-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`;
}

function normalizeProject(entry, idx) {
  if (!entry || typeof entry !== 'object') return null;
  const id = typeof entry.id === 'string' && entry.id.trim() ? entry.id.trim() : generateProjectId();
  const name = typeof entry.name === 'string' && entry.name.trim() ? entry.name.trim() : `Проект ${idx + 1}`;
  return {
    id,
    name,
    code: typeof entry.code === 'string' ? entry.code : '',
    args: typeof entry.args === 'string' ? entry.args : '',
    stdin: typeof entry.stdin === 'string' ? entry.stdin : '',
    files: Array.isArray(entry.files) ? entry.files.map((f, i) => normalizeIoFile(f, i)).filter(Boolean) : [],
    updatedAt: Number(entry.updatedAt) || Date.now()
  };
}

function bootstrapProjects() {
  if (__projects.length) return;
  let parsed = [];
  const raw = readPersistedValue(PROJECTS_STORAGE_KEY);
  if (raw) {
    try {
      const data = JSON.parse(raw);
      if (Array.isArray(data)) {
        parsed = data.map((entry, idx) => normalizeProject(entry, idx)).filter(Boolean);
      }
    } catch (err) {
      console.warn('projects parse failed:', err);
    }
  }
  if (!parsed.length) {
    const fallbackCode = readPersistedValue('q_code');
    const fallbackArgs = readPersistedValue('q_args');
    parsed = [normalizeProject({
      id: generateProjectId(),
      name: 'Проект 1',
      code: typeof fallbackCode === 'string' ? fallbackCode : sample,
      args: typeof fallbackArgs === 'string' ? fallbackArgs : '',
      stdin: '',
      files: [],
      updatedAt: Date.now()
    }, 0)];
  }
  __projects = parsed;
  const storedActive = readPersistedValue(ACTIVE_PROJECT_KEY);
  if (storedActive && __projects.some(p => p.id === storedActive)) {
    __activeProjectId = storedActive;
  } else {
    __activeProjectId = __projects.length ? __projects[0].id : null;
  }
  persistProjects();
}

function getActiveProject() {
  return __projects.find(p => p.id === __activeProjectId) || null;
}

function persistProjects() {
  if (!Array.isArray(__projects)) __projects = [];
  __projects.sort((a, b) => (b.updatedAt || 0) - (a.updatedAt || 0));
  writePersistedValue(PROJECTS_STORAGE_KEY, JSON.stringify(__projects));
  if (__activeProjectId) {
    writePersistedValue(ACTIVE_PROJECT_KEY, __activeProjectId);
  }
}

function captureCurrentEditorState() {
  const argsEl = $('#args');
  const stdinEl = $('#stdin');
  return {
    code: getCode(),
    args: argsEl ? argsEl.value : '',
    stdin: stdinEl ? stdinEl.value : ''
  };
}

function updateActiveProjectFromInputs() {
  const project = getActiveProject();
  if (!project) return;
  const snapshot = captureCurrentEditorState();
  // console.log('[projects] updateActiveProjectFromInputs before', {
  //   id: project.id,
  //   name: project.name,
  //   prev: { code: project.code, args: project.args, stdin: project.stdin },
  //   next: snapshot
  // });
  if (project.code === snapshot.code && project.args === snapshot.args && project.stdin === snapshot.stdin) {
    // console.log('[projects] updateActiveProjectFromInputs: no changes, skip');
    return;
  }
  project.code = snapshot.code;
  project.args = snapshot.args;
  project.stdin = snapshot.stdin;
  project.updatedAt = Date.now();
  // console.log('[projects] updateActiveProjectFromInputs after', {
  //   id: project.id,
  //   name: project.name,
  //   stdin: project.stdin
  // });
  persistProjects();
  scheduleProjectsRender();
}

function applyProjectToInputs(project, { silent = false } = {}) {
  if (!project) return;
  // console.log('[projects] applyProjectToInputs', {
  //   id: project.id,
  //   name: project.name,
  //   stdin: project.stdin
  // });
  setCode(typeof project.code === 'string' ? project.code : '');
  const argsEl = $('#args');
  if (argsEl) argsEl.value = project.args || '';
  const stdinEl = $('#stdin');
  if (stdinEl) stdinEl.value = project.stdin || '';
  // Restore per-project IO files into the workspace
  if (Array.isArray(project.files)) {
    if (__ioFilesRoot) {
      __ioFilesRoot.innerHTML = '';
    }
    __ioFiles.length = 0;
    project.files.forEach(file => {
      const f = normalizeIoFile(file, __ioFiles.length);
      if (!f) return;
      __ioFiles.push(f);
      renderIoFilePane(f);
    });
    refreshIoSelectOptions();
    // Restore active pane - if saved pane exists in this project's files, use it; otherwise stdout
    const savedPane = getCookie(IO_PANE_COOKIE) || 'stdout';
    const knownIds = new Set(['stdout', 'stdin', ...__ioFiles.map(f => f.id)]);
    const targetPane = knownIds.has(savedPane) ? savedPane : 'stdout';
    setActiveIoPane(targetPane, { persistCookie: false });
  }
  if (!silent) {
    saveState();
  }
  // Preview robot field if code uses robot and .fil file exists
  tryPreviewRobotField(project.code, project.files);
}

// Check if code likely uses robot (by looking for robot keywords)
function codeUsesRobot(code) {
  if (!code) return false;
  // Check for "использовать Робот" or robot function calls
  return /(использовать)\s+Робот/i.test(code) ||
         /\b(вверх|вниз|влево|вправо|закрасить|сверху_свободно|снизу_свободно|слева_свободно|справа_свободно|сверху_стена|снизу_стена|слева_стена|справа_стена|клетка_закрашена|клетка_чистая)\s*[\(\n]/i.test(code);
}

// Check if files contain a .fil file
function hasFilFile(files) {
  if (!Array.isArray(files)) return false;
  return files.some(f => f && typeof f.name === 'string' && f.name.toLowerCase().endsWith('.fil'));
}

// Try to preview robot field if applicable
async function tryPreviewRobotField(code, files) {
  if (!codeUsesRobot(code)) {
    // Hide robot UI if code doesn't use robot
    hideRobotUI();
    return;
  }

  // Ensure robot module is loaded
  if (!__robotModule) {
    try { __robotModule = await import('./runtime/robot.js'); } catch { return; }
  }

  // Setup canvas if needed
  ensureRobotUI();

  // Setup file accessor for robot module
  if (__robotModule && typeof __robotModule.__setRobotFilesAccessor === 'function') {
    __robotModule.__setRobotFilesAccessor(() => __ioFiles);
  }

  // Preview field
  if (__robotModule && typeof __robotModule.__previewField === 'function') {
    __robotModule.__previewField();
  }

  // Show robot canvas
  setCompilerOutputMode('robot');
}

// Update robot field preview when .fil file is edited (if robot view is active)
function tryUpdateRobotFieldPreview() {
  // Only update if robot mode is active
  if (__compilerOutputMode !== 'robot') return;
  if (!__robotModule) return;

  // Re-preview the field
  if (typeof __robotModule.__previewField === 'function') {
    __robotModule.__previewField();
  }
  renderRobotField();
}

function setActiveProject(projectId, { silent = false } = {}) {
  if (!projectId || projectId === __activeProjectId) return;
  // console.log('[projects] setActiveProject start', {
  //   from: __activeProjectId,
  //   to: projectId
  // });
  // Before switching, persist current editor state and IO files into the
  // active project so that each project keeps its own files and stdin.
  updateActiveProjectFromInputs();
  const current = getActiveProject();
  if (current) {
    // console.log('[projects] setActiveProject saving current files', {
    //   id: current.id,
    //   name: current.name,
    //   filesCount: __ioFiles.length,
    //   stdin: current.stdin
    // });
    current.files = __ioFiles.map((file, idx) => normalizeIoFile(file, idx)).filter(Boolean);
    current.updatedAt = Date.now();
    persistProjects();
  }
  if (!__projects.some(p => p.id === projectId)) return;
  __activeProjectId = projectId;
  persistProjects();
  scheduleProjectsRender();
  const target = getActiveProject();
  if (target) {
    // console.log('[projects] setActiveProject applying target', {
    //   id: target.id,
    //   name: target.name,
    //   stdin: target.stdin
    // });
    applyProjectToInputs(target, { silent });
  }
}

function createProject(initial = {}, { activate = true } = {}) {
  const name = typeof initial.name === 'string' && initial.name.trim() ? initial.name.trim() : `Проект ${__projects.length + 1}`;
  const project = {
    id: generateProjectId(),
    name,
    code: typeof initial.code === 'string' ? initial.code : '',
    args: typeof initial.args === 'string' ? initial.args : '',
    stdin: typeof initial.stdin === 'string' ? initial.stdin : '',
    files: Array.isArray(initial.files) ? initial.files.map((f, i) => normalizeIoFile(f, i)).filter(Boolean) : [],
    updatedAt: Date.now()
  };
  __projects.push(project);
  if (activate || !__activeProjectId) {
    __activeProjectId = project.id;
  }
  persistProjects();
  scheduleProjectsRender();
  if (activate) {
    applyProjectToInputs(project);
    closeProjectsDrawer();
  }
  return project;
}

function renameProject(projectId, nextName) {
  const project = __projects.find(p => p.id === projectId);
  if (!project) return;
  const trimmed = typeof nextName === 'string' ? nextName.trim() : '';
  if (!trimmed || trimmed === project.name) return;
  project.name = trimmed;
  project.updatedAt = Date.now();
  persistProjects();
  scheduleProjectsRender();
}

function deleteProject(projectId) {
  const idx = __projects.findIndex(p => p.id === projectId);
  if (idx === -1) return;
  __projects.splice(idx, 1);
  if (!__projects.length) {
    createProject({ name: 'Проект 1' }, { activate: true });
    return;
  }
  if (__activeProjectId === projectId) {
    const fallback = __projects[idx] || __projects[idx - 1] || __projects[0];
    __activeProjectId = fallback.id;
    persistProjects();
    scheduleProjectsRender();
    applyProjectToInputs(fallback);
  } else {
    persistProjects();
    scheduleProjectsRender();
  }
}

function formatProjectTimestamp(ts) {
  if (!ts) return '';
  try {
    const date = new Date(Number(ts));
    if (Number.isNaN(date.getTime())) return '';
    const now = new Date();
    const sameDay = date.toDateString() === now.toDateString();
    if (sameDay) {
      return date.toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit' });
    }
    return date.toLocaleDateString('ru-RU', { day: '2-digit', month: 'short' });
  } catch {
    return '';
  }
}

function renderProjectsList() {
  if (!__projectsListEl) {
    if (__projectActiveNameEl) {
      const active = getActiveProject();
      __projectActiveNameEl.textContent = active ? active.name : 'Проект';
    }
    return;
  }
  const activeId = __activeProjectId;
  const fragment = document.createDocumentFragment();
  __projects.forEach(project => {
    const row = document.createElement('div');
    row.className = 'project-row';
    if (project.id === activeId) row.classList.add('active');
    row.dataset.projectId = project.id;

    const main = document.createElement('div');
    main.className = 'project-row-main';
    const nameEl = document.createElement('div');
    nameEl.className = 'project-name';
    nameEl.textContent = project.name || 'Без имени';
    const metaEl = document.createElement('div');
    metaEl.className = 'project-meta';
    const stamp = formatProjectTimestamp(project.updatedAt);
    metaEl.textContent = stamp ? `обновл. ${stamp}` : ' ';
    main.appendChild(nameEl);
    main.appendChild(metaEl);

    const actions = document.createElement('div');
    actions.className = 'project-actions';
    const renameBtn = document.createElement('button');
    renameBtn.type = 'button';
    renameBtn.className = 'project-action';
    renameBtn.dataset.action = 'rename';
    renameBtn.title = 'Переименовать';
    renameBtn.textContent = '✎';
    const deleteBtn = document.createElement('button');
    deleteBtn.type = 'button';
    deleteBtn.className = 'project-action danger';
    deleteBtn.dataset.action = 'delete';
    deleteBtn.title = 'Удалить';
    deleteBtn.textContent = '✕';
    actions.appendChild(renameBtn);
    actions.appendChild(deleteBtn);

    row.appendChild(main);
    row.appendChild(actions);
    fragment.appendChild(row);
  });
  __projectsListEl.replaceChildren(fragment);
  if (__projectActiveNameEl) {
    const active = getActiveProject();
    __projectActiveNameEl.textContent = active ? active.name : 'Проект';
  }
}

function scheduleProjectsRender() {
  if (__projectsRenderPending) return;
  __projectsRenderPending = true;
  const runner = typeof requestAnimationFrame === 'function' ? requestAnimationFrame : (cb) => setTimeout(cb, 0);
  runner(() => {
    __projectsRenderPending = false;
    renderProjectsList();
  });
}

function deriveExampleProjectName(path) {
  if (!path) return 'Пример';
  const parts = path.split('/');
  const last = parts[parts.length - 1] || path;
  const trimmed = last.replace(/\.[^.]+$/, '');
  return trimmed || last || 'Пример';
}

function openProjectsDrawer() {
  document.body.classList.add('projects-open');
  if (__projectToggleBtn) __projectToggleBtn.setAttribute('aria-expanded', 'true');
}

function closeProjectsDrawer() {
  document.body.classList.remove('projects-open');
  if (__projectToggleBtn) __projectToggleBtn.setAttribute('aria-expanded', 'false');
}

function toggleProjectsDrawer() {
  if (document.body.classList.contains('projects-open')) {
    closeProjectsDrawer();
  } else {
    openProjectsDrawer();
  }
}

function generateIoFileId() {
  __ioFileCounter += 1;
  return `file-${Date.now().toString(36)}-${__ioFileCounter}`;
}

function normalizeIoFile(entry, idx) {
  if (!entry || typeof entry !== 'object') return null;
  const id = typeof entry.id === 'string' && entry.id.trim() ? entry.id.trim() : generateIoFileId();
  const defaultName = `file${idx + 1}`;
  const name = typeof entry.name === 'string' && entry.name.trim() ? entry.name.trim() : defaultName;
  const content = typeof entry.content === 'string' ? entry.content : '';
  return { id, name, content };
}

function canonicalIoFileName(name) {
  return (name || '').trim();
}

function createTextTokenStream(text) {
  const raw = String(text || '');
  const WHITESPACE = new Set([9, 10, 11, 12, 13, 32, 160]);
  let cursor = 0;

  const isWhitespace = (code) => WHITESPACE.has(code);

  const skipWhitespace = () => {
    while (cursor < raw.length && isWhitespace(raw.charCodeAt(cursor))) {
      cursor++;
    }
  };

  return {
    readToken() {
      skipWhitespace();
      if (cursor >= raw.length) return '0';
      const start = cursor;
      while (cursor < raw.length && !isWhitespace(raw.charCodeAt(cursor))) {
        cursor++;
      }
      const token = raw.slice(start, cursor);
      return token.length ? token : '0';
    },
    readLine() {
      if (cursor >= raw.length) return '';
      const newlineIndex = raw.indexOf('\n', cursor);
      let line;
      if (newlineIndex === -1) {
        line = raw.slice(cursor);
        cursor = raw.length;
      } else {
        line = raw.slice(cursor, newlineIndex);
        cursor = newlineIndex + 1;
      }
      if (line.endsWith('\r')) {
        line = line.slice(0, -1);
      }
      return line;
    },
    hasMore() {
      return cursor < raw.length;
    },
    reset() {
      cursor = 0;
    }
  };
}

function createBrowserFileManager(filesAccessor, { addFile, updateFile } = {}) {
  const handles = new Map();
  const freeHandles = [];
  let nextHandle = 1;

  const getFiles = () => (typeof filesAccessor === 'function' ? filesAccessor() : []);

  return {
    open(name) {
      const targetName = canonicalIoFileName(name);
      if (!targetName) return -1;
      const files = getFiles();
      const file = files.find(f => canonicalIoFileName(f.name) === targetName);
      if (!file) return -1;
      const handle = freeHandles.length ? freeHandles.pop() : nextHandle++;
      handles.set(handle, {
        name: targetName,
        stream: createTextTokenStream(file.content || ''),
        mode: 'read',
        fileId: file.id
      });
      return handle;
    },
    openForWrite(name) {
      const targetName = canonicalIoFileName(name);
      if (!targetName) return -1;
      const files = getFiles();
      let file = files.find(f => canonicalIoFileName(f.name) === targetName);
      if (file) {
        // Clear existing file
        file.content = '';
        if (typeof updateFile === 'function') updateFile(file.id, '');
      } else {
        // Create new file
        const newId = generateIoFileId();
        file = { id: newId, name: targetName, content: '' };
        if (typeof addFile === 'function') addFile(file);
      }
      const handle = freeHandles.length ? freeHandles.pop() : nextHandle++;
      handles.set(handle, {
        name: targetName,
        mode: 'write',
        fileId: file.id
      });
      return handle;
    },
    openForAppend(name) {
      const targetName = canonicalIoFileName(name);
      if (!targetName) return -1;
      const files = getFiles();
      let file = files.find(f => canonicalIoFileName(f.name) === targetName);
      if (!file) {
        // Create new file if doesn't exist
        const newId = generateIoFileId();
        file = { id: newId, name: targetName, content: '' };
        if (typeof addFile === 'function') addFile(file);
      }
      // Do NOT clear existing content
      const handle = freeHandles.length ? freeHandles.pop() : nextHandle++;
      handles.set(handle, {
        name: targetName,
        mode: 'write',  // same mode as write, just don't clear
        fileId: file.id
      });
      return handle;
    },
    write(handle, text) {
      const h = Number(handle) | 0;
      const slot = handles.get(h);
      if (!slot || slot.mode !== 'write') return;
      const files = getFiles();
      const file = files.find(f => f.id === slot.fileId);
      if (file) {
        file.content = (file.content || '') + String(text);
        if (typeof updateFile === 'function') updateFile(file.id, file.content);
      }
    },
    close(handle) {
      const h = Number(handle) | 0;
      if (handles.delete(h)) {
        freeHandles.push(h);
      }
    },
    hasMore(handle) {
      const slot = handles.get(Number(handle) | 0);
      if (!slot || !slot.stream || typeof slot.stream.hasMore !== 'function') return false;
      return slot.stream.hasMore();
    },
    getStream(handle) {
      const slot = handles.get(Number(handle) | 0);
      return slot ? slot.stream : null;
    },
    reset() {
      handles.clear();
      freeHandles.length = 0;
      nextHandle = 1;
    }
  };
}

function ensureRuntimeFileManager(ioRuntime) {
  if (!ioRuntime || typeof ioRuntime.setFileManager !== 'function') {
    return;
  }
  if (!__browserFileManager) {
    __browserFileManager = createBrowserFileManager(() => __ioFiles, {
      addFile(file) {
        __ioFiles.push(file);
        // Create DOM pane for new file
        renderIoFilePane(file);
        refreshIoSelectOptions();
        // Show the new file pane
        setActiveIoPane(file.id);
      },
      updateFile(fileId, content) {
        const file = __ioFiles.find(f => f.id === fileId);
        if (file) {
          file.content = content;
          // Update the textarea if pane exists
          if (file.elements && file.elements.editor) {
            file.elements.editor.value = content;
          }
        }
      }
    });
  }
  ioRuntime.setFileManager(__browserFileManager);
}

function refreshIoSelectOptions() {
  if (!__ioSelectEl) return;
  const fragment = document.createDocumentFragment();
  const addOption = (value, label) => {
    const opt = document.createElement('option');
    opt.value = value;
    opt.textContent = label;
    fragment.appendChild(opt);
  };
  addOption('stdout', 'stdout');
  addOption('stdin', 'stdin');
  addOption('errors', 'errors');
  __ioFiles.forEach(file => {
    const label = file.name && file.name.trim() ? file.name.trim() : 'untitled';
    addOption(file.id, label);
  });
  __ioSelectEl.replaceChildren(fragment);
  const knownIds = new Set(['stdout', 'stdin', 'errors', ...__ioFiles.map(f => f.id)]);
  const target = knownIds.has(__currentIoPane) ? __currentIoPane : 'stdout';
  __ioSelectEl.value = target;
}

function setActiveIoPane(candidate, { persistCookie = true } = {}) {
  const knownIds = new Set(['stdout', 'stdin', 'errors', ...__ioFiles.map(f => f.id)]);
  const target = knownIds.has(candidate) ? candidate : 'stdout';
  __currentIoPane = target;
  if (__ioSelectEl && __ioSelectEl.value !== target) {
    __ioSelectEl.value = target;
  }
  document.querySelectorAll('.io-pane').forEach(node => {
    node.classList.toggle('active', node.dataset.ioPane === target);
  });
  if (persistCookie) {
    setCookie(IO_PANE_COOKIE, target);
  }
}

function renderIoFilePane(file) {
  if (!__ioFilesRoot) return;
  const pane = document.createElement('div');
  pane.className = 'io-pane io-file-pane';
  pane.dataset.ioPane = file.id;

  const meta = document.createElement('div');
  meta.className = 'io-file-meta';

  const nameInput = document.createElement('input');
  nameInput.type = 'text';
  nameInput.className = 'io-file-name';
  nameInput.placeholder = 'Имя файла';
  nameInput.value = file.name;
  nameInput.setAttribute('aria-label', 'Имя файла');

  const removeBtn = document.createElement('button');
  removeBtn.type = 'button';
  removeBtn.className = 'io-icon danger';
  removeBtn.title = 'Удалить файл';
  removeBtn.textContent = '×';
  removeBtn.setAttribute('aria-label', 'Удалить файл');

  meta.appendChild(nameInput);
  meta.appendChild(removeBtn);

  const editor = document.createElement('textarea');
  editor.className = 'io-file-text';
  editor.placeholder = 'Содержимое файла';
  editor.spellcheck = false;
  editor.value = file.content;

  pane.appendChild(meta);
  pane.appendChild(editor);
  __ioFilesRoot.appendChild(pane);

  file.elements = { pane, nameInput, editor };

  const commitName = (value) => {
    const wasFil = file.name && file.name.toLowerCase().endsWith('.fil');
    file.name = value;
    refreshIoSelectOptions();
    persistIoFiles();
    // Update robot preview if file became or stopped being .fil
    const isFil = file.name && file.name.toLowerCase().endsWith('.fil');
    if (isFil || wasFil) {
      tryUpdateRobotFieldPreview();
    }
  };

  nameInput.addEventListener('input', () => {
    commitName(nameInput.value);
  });

  nameInput.addEventListener('blur', () => {
    const trimmed = nameInput.value.trim();
    const finalValue = trimmed || 'untitled';
    if (finalValue !== file.name || finalValue !== nameInput.value) {
      nameInput.value = finalValue;
      commitName(finalValue);
    }
  });

  removeBtn.addEventListener('click', () => removeIoFile(file.id));

  editor.addEventListener('input', () => {
    file.content = editor.value;
    // If this is a .fil file and robot view is active, update the field preview
    if (file.name && file.name.toLowerCase().endsWith('.fil')) {
      tryUpdateRobotFieldPreview();
    }
  });
}

function removeIoFile(fileId) {
  const idx = __ioFiles.findIndex(f => f.id === fileId);
  if (idx === -1) return;
  const [file] = __ioFiles.splice(idx, 1);
  if (file && file.elements && file.elements.pane && file.elements.pane.parentNode) {
    file.elements.pane.parentNode.removeChild(file.elements.pane);
  }
  if (__currentIoPane === fileId) {
    setActiveIoPane('stdout');
  }
  refreshIoSelectOptions();
}

function initIoWorkspace() {
  __ioSelectEl = document.getElementById('io-select');
  __ioFilesRoot = document.getElementById('io-files');
  const addBtn = document.getElementById('io-add-file');
  if (!__ioSelectEl || !__ioFilesRoot || !addBtn) return;

  // IO files are restored from the active project in applyProjectToInputs().
  refreshIoSelectOptions();

  __ioSelectEl.addEventListener('change', () => setActiveIoPane(__ioSelectEl.value));

  addBtn.addEventListener('click', () => {
    const newFile = {
      id: generateIoFileId(),
      name: `file${__ioFiles.length + 1}`,
      content: ''
    };
    __ioFiles.push(newFile);
    renderIoFilePane(newFile);
    refreshIoSelectOptions();
    setActiveIoPane(newFile.id);
    if (newFile.elements && newFile.elements.nameInput) {
      newFile.elements.nameInput.focus();
      newFile.elements.nameInput.select();
    }
  });

  setActiveIoPane(__currentIoPane, { persistCookie: false });

  // Create Errors pane placeholder
  ensureErrorsPane();
}

// Ensure a dedicated read-only Errors pane exists under IO
function ensureErrorsPane() {
  if (!__ioFilesRoot) return;
  let pane = document.querySelector('.io-pane.errors-pane');
  if (pane) return;
  pane = document.createElement('div');
  pane.className = 'io-pane errors-pane';
  pane.dataset.ioPane = 'errors';
  const viewer = document.createElement('pre');
  viewer.id = 'errors';
  viewer.className = 'io-file-text';
  viewer.style.whiteSpace = 'pre-wrap';
  viewer.style.userSelect = 'text';
  viewer.setAttribute('aria-label', 'Ошибки');
  pane.appendChild(viewer);
  __ioFilesRoot.appendChild(pane);
}

function setErrorsPaneContent(text) {
  ensureErrorsPane();
  const viewer = document.getElementById('errors');
  if (viewer) {
    viewer.textContent = text || '';
  }
}

function initProjectsUI() {
  __projectToggleBtn = document.getElementById('project-toggle');
  __projectActiveNameEl = document.getElementById('project-active-name');
  __projectsDrawer = document.getElementById('projects-drawer');
  __projectsBackdrop = document.getElementById('projects-backdrop');
  __projectsListEl = document.getElementById('projects-list');
  __projectNewBtn = document.getElementById('project-new');
  const projectCloseBtn = document.getElementById('project-close');

  if (__projectToggleBtn) {
    __projectToggleBtn.addEventListener('click', toggleProjectsDrawer);
  }
  if (__projectsBackdrop) {
    __projectsBackdrop.addEventListener('click', closeProjectsDrawer);
  }
  if (projectCloseBtn) {
    projectCloseBtn.addEventListener('click', closeProjectsDrawer);
  }
  if (__projectNewBtn) {
    __projectNewBtn.addEventListener('click', () => {
      updateActiveProjectFromInputs();
      createProject({ name: `Проект ${__projects.length + 1}` }, { activate: true });
    });
  }
  if (__projectsListEl) {
    __projectsListEl.addEventListener('click', (event) => {
      const row = event.target.closest('.project-row');
      if (!row) return;
      const projectId = row.dataset.projectId;
      if (!projectId) return;
      const actionBtn = event.target.closest('button[data-action]');
      const project = __projects.find(p => p.id === projectId);
      if (actionBtn) {
        const action = actionBtn.dataset.action;
        if (action === 'rename') {
          const suggested = project && project.name ? project.name : '';
          const nextName = typeof window !== 'undefined' ? window.prompt('Имя проекта', suggested) : null;
          if (nextName !== null) renameProject(projectId, nextName);
        } else if (action === 'delete') {
          const label = project && project.name ? `"${project.name}"` : '';
          const question = label ? `Удалить проект ${label}?` : 'Удалить проект?';
          const approved = typeof window === 'undefined' ? true : window.confirm(question);
          if (approved) deleteProject(projectId);
        }
        return;
      }
      const switching = projectId !== __activeProjectId;
      if (switching) {
        setActiveProject(projectId);
        if (!editor) debounceShow();
      }
      closeProjectsDrawer();
    });
  }
  if (typeof window !== 'undefined') {
    window.addEventListener('keydown', (event) => {
      if (event.key === 'Escape' && document.body.classList.contains('projects-open')) {
        closeProjectsDrawer();
      }
    });
  }
  renderProjectsList();
}

function getCurrentIoPaneNode() {
  if (__currentIoPane === 'stdout') return document.getElementById('stdout');
  if (__currentIoPane === 'stdin') return document.getElementById('stdin');
  if (__currentIoPane === 'errors') return document.getElementById('errors');
  const file = __ioFiles.find(f => f.id === __currentIoPane);
  return file && file.elements ? file.elements.editor : null;
}

function getCurrentIoPaneLabel() {
  if (__currentIoPane === 'stdout') return 'stdout';
  if (__currentIoPane === 'stdin') return 'stdin';
  if (__currentIoPane === 'errors') return 'errors';
  const file = __ioFiles.find(f => f.id === __currentIoPane);
  if (!file) return 'file';
  return file.name && file.name.trim() ? file.name.trim() : 'file';
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

// Helper: collect multiline error text until next error header
function collectErrorText(lines, startIdx, initialText = '') {
  let text = initialText;
  let i = startIdx;
  while (i < lines.length) {
    const nextLine = lines[i];
    // Stop if we hit another error line
    if (/^Error:\s*/.test(nextLine) || /^Строка:\s*\d+/.test(nextLine)) {
      break;
    }
    // Append this line to the error text
    text += (text ? '\n' : '') + nextLine;
    i++;
  }
  return { text, nextIdx: i };
}

// Format compiler error lines like:
// "Error: <text> @ Line: N, Byte: B, Column: C"
// into:
// "Строка: N, Колонка: C\n   <spaces>Ошибка"
function formatCompilerErrors(payload) {
  if (typeof payload !== 'string') return payload;
  const lines = payload.split(/\r?\n/);
  const re = /^Error:\s*(.+?)\s*@\s*Line:\s*(\d+),\s*Byte:\s*\d+,\s*Column:\s*(\d+)/;
  const blocks = [];
  for (let i = 0; i < lines.length; i++) {
    const m = re.exec(lines[i]);
    if (!m) continue;
    const lineNum = Number(m[2]) || 0;
    const colNum = Number(m[3]) || 0;
    // Collect subsequent lines until next error or end
    const { text, nextIdx } = collectErrorText(lines, i + 1, m[1]);
    i = nextIdx - 1; // -1 because loop will increment
    blocks.push(`Строка: ${lineNum}, Колонка: ${colNum}\n  ${text}`);
  }
  // If we detected any error lines, return the formatted blocks joined by newlines;
  // otherwise, keep the original payload.
  return blocks.length ? blocks.join('\n') : payload;
}

// Parse compiler errors into { line, col, text }
function parseCompilerErrors(payload) {
  if (typeof payload !== 'string') return [];
  const lines = payload.split(/\r?\n/);
  const errs = [];
  const reA = /^Error:\s*(.+?)\s*@\s*Line:\s*(\d+),\s*Byte:\s*\d+,\s*Column:\s*(\d+)/;
  for (let i = 0; i < lines.length; i++) {
    const a = reA.exec(lines[i]);
    if (a) {
      const lineNum = Number(a[2]) || 0;
      const colNum = Number(a[3]) || 0;
      // Collect subsequent lines until next error
      const { text, nextIdx } = collectErrorText(lines, i + 1, a[1]);
      i = nextIdx - 1;
      errs.push({ line: lineNum, col: colNum, text });
      continue;
    }
    const head = /^Строка:\s*(\d+)\s*,\s*Колонка:\s*(\d+)/.exec(lines[i]);
    if (head && i + 1 < lines.length) {
      const bodyLine = lines[i + 1] || '';
      const text = bodyLine.replace(/^\s+/, '');
      errs.push({ line: Number(head[1]) || 0, col: Number(head[2]) || 0, text });
      i++; // Skip the body line
    }
  }
  return errs;
}

let __errorMarks = [];
function clearErrorHighlights() {
  if (editor && __errorMarks && __errorMarks.length) {
    for (const mk of __errorMarks) {
      try { mk.clear(); } catch (_) {}
    }
  }
  __errorMarks = [];
  // Clear line classes
  if (editor && typeof editor.eachLine === 'function') {
    try {
      editor.eachLine((h) => { try { editor.removeLineClass(h, 'background', 'q-error-line'); } catch (_) {} });
    } catch (_) {}
  }
  // Clear gutter markers
  if (editor && typeof editor.clearGutter === 'function') {
    try { editor.clearGutter('q-error-gutter'); } catch (_) {}
  }
}

function addErrorHighlights(errors) {
  if (!Array.isArray(errors) || !errors.length) return;
  if (!editor || typeof editor.getDoc !== 'function') return;
  const doc = editor.getDoc();
  clearErrorHighlights();
  ensureErrorGutter();
  for (const err of errors) {
    const lineIdx = Math.max(0, (err.line || 1) - 1);
    const chIdx = Math.max(0, (err.col || 1) - 1);
    const from = { line: lineIdx, ch: chIdx };
    const lineText = doc.getLine(lineIdx) || '';
    let endCh = chIdx;
    while (endCh < lineText.length && lineText[endCh] === ' ') endCh++;
    while (endCh < lineText.length && /[^\s\t\n\r]/.test(lineText[endCh])) endCh++;
    if (endCh === chIdx) endCh = Math.min(lineText.length, chIdx + 1);
    const to = { line: lineIdx, ch: endCh };
    const mark = doc.markText(from, to, {
      className: 'q-error-mark',
      // Use data-error only to avoid native title tooltip duplication
      attributes: { 'data-error': err.text || 'Ошибка' }
    });
    __errorMarks.push(mark);
    try { editor.addLineClass(lineIdx, 'background', 'q-error-line'); } catch (_) {}

    // Add gutter marker (red dot) on the line
    try {
      const dot = document.createElement('div');
      dot.className = 'q-error-dot';
      // Avoid native title to prevent double tooltip; fast tooltip reads data-error
      dot.setAttribute('data-error', err.text || 'Ошибка');
      editor.setGutterMarker(lineIdx, 'q-error-gutter', dot);
    } catch (_) {}
  }
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
      clearErrorHighlights();
      setErrorsPaneContent('Успешно');
    } else {
      const formatted = formatCompilerErrors(data);
      $('#output').textContent = formatted;
      const errs = parseCompilerErrors(data);
      if (errs.length) addErrorHighlights(errs); else clearErrorHighlights();
      const errorsText = errs.length ? formatted : 'Успешно';
      setErrorsPaneContent(errorsText);
    }
  } catch (e) {
    if (e.name === 'AbortError') return;
    const msg = typeof e?.message === 'string' ? e.message : String(e);
    const formatted = formatCompilerErrors(msg);
    $('#output').textContent = formatted;
    const errs = parseCompilerErrors(msg);
    if (errs.length) addErrorHighlights(errs); else clearErrorHighlights();
    $('#output').classList.add('error');
    const errorsText = errs.length ? formatted : 'Успешно';
    setErrorsPaneContent(errorsText);
  }
}

// Ensure basic styles exist for error highlights
(function ensureErrorStyles(){
  if (typeof document === 'undefined') return;
  const id = 'q-error-styles';
  if (document.getElementById(id)) return;
  const style = document.createElement('style');
  style.id = id;
  style.textContent = `
    .q-error-line { background: rgba(255, 0, 0, 0.08) !important; }
    .q-error-mark { background: rgba(255, 0, 0, 0.18); border-bottom: 1px solid rgba(255,0,0,0.6); }
    .CodeMirror-gutters { border-right: 1px solid #e0e0e0; }
    /* Make error gutter compact so dot sits close to line number */
    .CodeMirror-gutter.q-error-gutter { width: 10px; }
    .q-error-dot { width: 8px; height: 8px; border-radius: 50%; background: #e53935; box-shadow: 0 0 0 1px rgba(0,0,0,0.1); margin: 0 auto; margin-top: 4px; }
  `;
  document.head.appendChild(style);
})();

// Ensure a dedicated gutter for error markers exists on the editor
function ensureErrorGutter() {
  if (!editor || typeof editor.setOption !== 'function') return;
  const existing = editor.getOption && editor.getOption('gutters');
  const gutters = Array.isArray(existing) ? existing.slice() : [];
  // Ensure both gutters and order: error gutter first (left), then line numbers
  const hasErr = gutters.includes('q-error-gutter');
  const hasLine = gutters.includes('CodeMirror-linenumbers');
  const next = [];
  if (!hasErr) next.push('q-error-gutter'); else next.push('q-error-gutter');
  if (!hasLine) next.push('CodeMirror-linenumbers'); else next.push('CodeMirror-linenumbers');
  // Append any remaining existing gutters preserving order
  for (const g of gutters) {
    if (g !== 'q-error-gutter' && g !== 'CodeMirror-linenumbers') next.push(g);
  }
  editor.setOption('gutters', next);
}

// Fast, JS-driven tooltip over error marks (appears quicker than native title)
(function enableFastErrorTooltip(){
  if (typeof document === 'undefined') return;
  let tipEl = null;
  const showTip = (target, text) => {
    if (!text) return;
    if (!tipEl) {
      tipEl = document.createElement('div');
      tipEl.className = 'q-tooltip';
      tipEl.style.position = 'fixed';
      tipEl.style.zIndex = '9999';
      tipEl.style.background = '#222';
      tipEl.style.color = '#fff';
      tipEl.style.padding = '6px 8px';
      tipEl.style.borderRadius = '4px';
      tipEl.style.boxShadow = '0 2px 8px rgba(0,0,0,0.25)';
      tipEl.style.fontSize = '12px';
      tipEl.style.pointerEvents = 'none';
      document.body.appendChild(tipEl);
    }
    tipEl.textContent = text;
    tipEl.style.display = 'block';
  };
  const hideTip = () => { if (tipEl) tipEl.style.display = 'none'; };

  // Listen on editor wrapper for hover over error marks or dots
  const attach = () => {
    if (!editor || !editor.getWrapperElement) return;
    const wrap = editor.getWrapperElement();
    if (!wrap) return;
    let hoverTimer = null;
    const delay = 120; // faster than default title
    wrap.addEventListener('mousemove', (ev) => {
      const target = ev.target;
      const isMark = target.classList && target.classList.contains('q-error-mark');
      const isDot = target.classList && target.classList.contains('q-error-dot');
      if (!(isMark || isDot)) { hideTip(); return; }
      const text = target.getAttribute('data-error') || '';
      clearTimeout(hoverTimer);
      hoverTimer = setTimeout(() => {
        showTip(target, text);
        const r = target.getBoundingClientRect();
        const pad = 8;
        const top = r.top - (tipEl ? tipEl.offsetHeight : 20) - pad;
        const left = Math.max(8, Math.min(window.innerWidth - (tipEl ? tipEl.offsetWidth : 100) - 8, r.left + r.width / 2 - ((tipEl ? tipEl.offsetWidth : 100) / 2)));
        if (tipEl) { tipEl.style.top = `${top}px`; tipEl.style.left = `${left}px`; }
      }, delay);
    });
    wrap.addEventListener('mouseleave', () => { hideTip(); });
  };
  // Defer attach slightly in case editor is initialized later
  const readyCheck = () => { if (editor) attach(); else setTimeout(readyCheck, 200); };
  readyCheck();
})();
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
    out.parentNode.insertBefore(ctr, out);
    __turtleToggle = ctr;
  }

  // Rebuild options for turtle mode
  __turtleToggle.innerHTML = '';
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
  __turtleToggle.appendChild(makeOpt('Текст', 'text'));
  __turtleToggle.appendChild(makeOpt('Черепаха', 'turtle'));

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

// Robot UI functions
function ensureRobotUI() {
  const out = document.getElementById('output');
  if (!out) return;

  // Reuse turtle toggle if exists, just add robot option
  if (!__turtleToggle) {
    const ctr = document.createElement('div');
    ctr.id = 'output-mode';
    ctr.className = 'output-mode';
    ctr.style.margin = '6px 0';
    ctr.style.display = 'flex';
    ctr.style.gap = '12px';
    ctr.style.alignItems = 'center';
    ctr.style.flexWrap = 'wrap';
    out.parentNode.insertBefore(ctr, out);
    __turtleToggle = ctr;
  }

  // Ensure we have text and robot options
  __turtleToggle.innerHTML = '';
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
  __turtleToggle.appendChild(makeOpt('Текст', 'text'));
  __turtleToggle.appendChild(makeOpt('Робот', 'robot'));

  // Add animation speed control
  const speedContainer = document.createElement('div');
  speedContainer.style.display = 'flex';
  speedContainer.style.alignItems = 'center';
  speedContainer.style.gap = '6px';
  speedContainer.style.marginLeft = 'auto';

  // Checkbox to disable animation
  const animCheckbox = document.createElement('input');
  animCheckbox.type = 'checkbox';
  animCheckbox.id = 'robot-anim-enabled';
  const savedAnimEnabled = getCookie('q_robot_anim');
  animCheckbox.checked = savedAnimEnabled !== '0'; // enabled by default
  animCheckbox.style.cursor = 'pointer';
  animCheckbox.title = 'Включить анимацию';

  const speedLabel = document.createElement('span');
  speedLabel.textContent = '🐢';
  speedLabel.style.fontSize = '14px';

  const speedSlider = document.createElement('input');
  speedSlider.type = 'range';
  speedSlider.id = 'robot-speed';
  speedSlider.min = '0';
  speedSlider.max = '300';
  // Restore saved value or default to 150
  const savedSpeed = getCookie('q_robot_speed');
  speedSlider.value = savedSpeed !== null ? savedSpeed : '150';
  speedSlider.style.width = '80px';
  speedSlider.style.cursor = 'pointer';
  speedSlider.title = 'Скорость анимации';
  speedSlider.disabled = !animCheckbox.checked;
  speedSlider.style.opacity = animCheckbox.checked ? '1' : '0.5';

  const speedLabelFast = document.createElement('span');
  speedLabelFast.textContent = '🐇';
  speedLabelFast.style.fontSize = '14px';

  // Update delay based on checkbox and slider
  const updateAnimationDelay = () => {
    if (__robotModule && typeof __robotModule.__setAnimationDelay === 'function') {
      if (animCheckbox.checked) {
        __robotModule.__setAnimationDelay(300 - parseInt(speedSlider.value, 10));
      } else {
        __robotModule.__setAnimationDelay(0); // instant
      }
    }
  };

  animCheckbox.addEventListener('change', () => {
    speedSlider.disabled = !animCheckbox.checked;
    speedSlider.style.opacity = animCheckbox.checked ? '1' : '0.5';
    speedLabel.style.opacity = animCheckbox.checked ? '1' : '0.5';
    speedLabelFast.style.opacity = animCheckbox.checked ? '1' : '0.5';
    setCookie('q_robot_anim', animCheckbox.checked ? '1' : '0', 365);
    updateAnimationDelay();
  });

  speedSlider.addEventListener('input', () => {
    updateAnimationDelay();
    setCookie('q_robot_speed', speedSlider.value, 365);
  });

  // Apply initial opacity
  speedLabel.style.opacity = animCheckbox.checked ? '1' : '0.5';
  speedLabelFast.style.opacity = animCheckbox.checked ? '1' : '0.5';

  // Apply saved delay to module
  updateAnimationDelay();

  speedContainer.appendChild(animCheckbox);
  speedContainer.appendChild(speedLabel);
  speedContainer.appendChild(speedSlider);
  speedContainer.appendChild(speedLabelFast);

  __turtleToggle.appendChild(speedContainer);
  __turtleToggle.style.display = '';

  // Sync radios with current mode or saved cookie
  const saved = getCookie('q_out_mode');
  const targetMode = (saved === 'robot') ? 'robot' : __compilerOutputMode;
  const radios = __turtleToggle.querySelectorAll('input[name="q-out-mode"]');
  radios.forEach(r => { r.checked = (r.value === targetMode); });

  if (!__robotCanvas) {
    const cnv = document.createElement('canvas');
    cnv.id = 'robot-canvas';
    cnv.style.display = 'none';
    cnv.style.width = '100%';
    cnv.style.height = (out.clientHeight > 0 ? out.clientHeight + 'px' : (out.style.height || '300px'));
    cnv.style.background = '#fff';
    cnv.style.border = '1px solid #2b2b2b44';
    cnv.style.borderRadius = '4px';
    out.parentNode.insertBefore(cnv, out.nextSibling);
    __robotCanvas = cnv;
    if (window.ResizeObserver) {
      const ro = new ResizeObserver(() => {
        if (__robotCanvas && out) {
          const h = out.clientHeight;
          if (h > 32) __robotCanvas.style.height = h + 'px';
        }
      });
      try { ro.observe(out); } catch {}
    }
  } else {
    if (out.clientHeight > 32) __robotCanvas.style.height = out.clientHeight + 'px';
  }
}

function hideRobotUI() {
  // Stop any running animation
  if (__robotModule && typeof __robotModule.__stopAnimation === 'function') {
    __robotModule.__stopAnimation();
  }
  if (__robotCanvas) __robotCanvas.style.display = 'none';
  // Also hide toggle and restore text output
  if (__turtleToggle) __turtleToggle.style.display = 'none';
  const out = document.getElementById('output');
  if (out) out.style.display = '';
}

function renderRobotField() {
  if (!__robotCanvas || !__robotModule || !__robotModule.field) return;

  const field = __robotModule.field;
  const canvas = __robotCanvas;
  const ctx = canvas.getContext('2d');

  // Set actual canvas size for crisp rendering
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  canvas.width = rect.width * dpr;
  canvas.height = rect.height * dpr;
  ctx.scale(dpr, dpr);

  const w = rect.width;
  const h = rect.height;

  // Calculate cell size (reserve space for coordinate labels on all sides)
  const labelSpace = 20; // Space for coordinate numbers
  const padding = 5;
  const availW = w - 2 * padding - 2 * labelSpace;
  const availH = h - 2 * padding - 2 * labelSpace;
  const cellW = Math.floor(availW / field.width);
  const cellH = Math.floor(availH / field.height);
  const cellSize = Math.min(cellW, cellH, 40); // Max 40px per cell

  const gridW = cellSize * field.width;
  const gridH = cellSize * field.height;
  // Center grid in available area (between label spaces)
  const offsetX = padding + labelSpace + (availW - gridW) / 2;
  const offsetY = padding + labelSpace + (availH - gridH) / 2;

  // Clear
  ctx.fillStyle = '#fff';
  ctx.fillRect(0, 0, w, h);

  // Draw painted cells
  ctx.fillStyle = '#a8d4a8';
  for (const key of field.painted) {
    const [x, y] = key.split(',').map(Number);
    ctx.fillRect(offsetX + x * cellSize, offsetY + y * cellSize, cellSize, cellSize);
  }

  // Draw grid lines
  ctx.strokeStyle = '#ccc';
  ctx.lineWidth = 1;
  for (let x = 0; x <= field.width; x++) {
    ctx.beginPath();
    ctx.moveTo(offsetX + x * cellSize, offsetY);
    ctx.lineTo(offsetX + x * cellSize, offsetY + gridH);
    ctx.stroke();
  }
  for (let y = 0; y <= field.height; y++) {
    ctx.beginPath();
    ctx.moveTo(offsetX, offsetY + y * cellSize);
    ctx.lineTo(offsetX + gridW, offsetY + y * cellSize);
    ctx.stroke();
  }

  // Draw coordinate labels on all four sides
  ctx.fillStyle = '#666';
  ctx.font = `${Math.min(12, cellSize * 0.4)}px sans-serif`;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';

  // X coordinates (top and bottom)
  for (let x = 0; x < field.width; x++) {
    const cx = offsetX + x * cellSize + cellSize / 2;
    // Top
    ctx.fillText(String(x), cx, offsetY - labelSpace / 2);
    // Bottom
    ctx.fillText(String(x), cx, offsetY + gridH + labelSpace / 2);
  }

  // Y coordinates (left and right)
  for (let y = 0; y < field.height; y++) {
    const cy = offsetY + y * cellSize + cellSize / 2;
    // Left
    ctx.fillText(String(y), offsetX - labelSpace / 2, cy);
    // Right
    ctx.fillText(String(y), offsetX + gridW + labelSpace / 2, cy);
  }

  // Draw outer border (thick)
  ctx.strokeStyle = '#333';
  ctx.lineWidth = 3;
  ctx.strokeRect(offsetX, offsetY, gridW, gridH);

  // Draw walls (thick black lines)
  ctx.strokeStyle = '#333';
  ctx.lineWidth = 3;
  ctx.lineCap = 'round';

  // Horizontal walls (hWalls: "x,y" = wall below cell (x,y))
  for (const key of field.hWalls) {
    const [x, y] = key.split(',').map(Number);
    const px = offsetX + x * cellSize;
    const py = offsetY + (y + 1) * cellSize;
    ctx.beginPath();
    ctx.moveTo(px, py);
    ctx.lineTo(px + cellSize, py);
    ctx.stroke();
  }

  // Vertical walls (vWalls: "x,y" = wall to the right of cell (x,y))
  for (const key of field.vWalls) {
    const [x, y] = key.split(',').map(Number);
    const px = offsetX + (x + 1) * cellSize;
    const py = offsetY + y * cellSize;
    ctx.beginPath();
    ctx.moveTo(px, py);
    ctx.lineTo(px, py + cellSize);
    ctx.stroke();
  }

  // Draw robot
  const rx = offsetX + field.robotX * cellSize + cellSize / 2;
  const ry = offsetY + field.robotY * cellSize + cellSize / 2;
  const robotRadius = cellSize * 0.35;

  // Robot body (blue circle)
  ctx.fillStyle = '#4a90d9';
  ctx.beginPath();
  ctx.arc(rx, ry, robotRadius, 0, Math.PI * 2);
  ctx.fill();

  // Robot outline
  ctx.strokeStyle = '#2563a0';
  ctx.lineWidth = 2;
  ctx.stroke();

  // Robot "eye" (direction indicator - for now just a dot)
  ctx.fillStyle = '#fff';
  ctx.beginPath();
  ctx.arc(rx, ry - robotRadius * 0.3, robotRadius * 0.25, 0, Math.PI * 2);
  ctx.fill();
}

function setCompilerOutputMode(mode) {
  __compilerOutputMode = (mode === 'turtle' || mode === 'robot') ? mode : 'text';
  setCookie('q_out_mode', __compilerOutputMode);
  const out = document.getElementById('output');

  // Hide all special canvases first
  if (__turtleCanvas) __turtleCanvas.style.display = 'none';
  if (__robotCanvas) __robotCanvas.style.display = 'none';

  if (__compilerOutputMode === 'turtle') {
    if (out) out.style.display = 'none';
    if (__turtleCanvas) __turtleCanvas.style.display = '';
    if (__turtleCanvas && out && out.clientHeight > 32) {
      __turtleCanvas.style.height = out.clientHeight + 'px';
    }
    try { if (__turtleModule && typeof __turtleModule.__onCanvasShown === 'function') __turtleModule.__onCanvasShown(); } catch {}
  } else if (__compilerOutputMode === 'robot') {
    if (out) out.style.display = 'none';
    if (__robotCanvas) __robotCanvas.style.display = '';
    if (__robotCanvas && out && out.clientHeight > 32) {
      __robotCanvas.style.height = out.clientHeight + 'px';
    }
    renderRobotField();
  } else {
    if (out) out.style.display = '';
  }
  if (__turtleToggle) {
    const radios = __turtleToggle.querySelectorAll('input[name="q-out-mode"]');
    radios.forEach(r => { r.checked = (r.value === __compilerOutputMode); });
  }
}

function getCurrentCompilerOutputNode() {
  if (__compilerOutputMode === 'turtle' && __turtleCanvas) return __turtleCanvas;
  if (__compilerOutputMode === 'robot' && __robotCanvas) return __robotCanvas;
  return document.getElementById('output');
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
    ensureRuntimeFileManager(ioEnv);
    const stringEnv = await import('./runtime/string.js');
    const arrayEnv = await import('./runtime/array.js');
    if (!__turtleModule) { try { __turtleModule = await import('./runtime/turtle.js'); } catch {} }
    if (!__robotModule) { try { __robotModule = await import('./runtime/robot.js'); } catch {} }
    const env = { ...mathEnv, ...ioEnv, ...stringEnv, ...arrayEnv, ...(__turtleModule || {}), ...(__robotModule || {}) };
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
    let usesRobot = false;
    try {
      const imps = module ? WebAssembly.Module.imports(module) : [];
      usesTurtle = Array.isArray(imps) && imps.some(imp => imp && imp.module === 'env' && typeof imp.name === 'string' && imp.name.startsWith('turtle_'));
      usesRobot = Array.isArray(imps) && imps.some(imp => imp && imp.module === 'env' && typeof imp.name === 'string' && imp.name.startsWith('robot_'));
    } catch {}
    if (usesTurtle && __turtleModule) {
      hideRobotUI();  // скрыть робота при переключении на черепаху
      ensureTurtleUI();
      if (__turtleCanvas && typeof __turtleModule.__bindTurtleCanvas === 'function') {
        __turtleModule.__bindTurtleCanvas(__turtleCanvas);
      }
      if (typeof __turtleModule.__resetTurtle === 'function') {
        __turtleModule.__resetTurtle(true);
      }
      const saved = getCookie('q_out_mode');
      setCompilerOutputMode(saved === 'turtle' ? 'turtle' : 'turtle');
    } else if (usesRobot && __robotModule) {
      hideTurtleUI();  // скрыть черепаху при переключении на робота
      ensureRobotUI();
      const saved = getCookie('q_out_mode');
      setCompilerOutputMode(saved === 'robot' ? 'robot' : 'robot');
    } else {
      hideTurtleUI();
      hideRobotUI();
      setCompilerOutputMode('text');
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
    // Robot integration: setup file accessor and init field only if program uses robot
    if (usesRobot && __robotModule) {
      if (typeof __robotModule.__setRobotFilesAccessor === 'function') {
        __robotModule.__setRobotFilesAccessor(
          () => __ioFiles,
          (file) => {
            // addFile callback
            const newId = generateIoFileId();
            const f = { id: newId, name: file.name, content: file.content || '' };
            __ioFiles.push(f);
            renderIoFilePane(f);
            refreshIoSelectOptions();
          },
          (fileId, content) => {
            // updateFile callback
            const file = __ioFiles.find(f => f.id === fileId);
            if (file) {
              file.content = content;
              if (file.elements && file.elements.editor) {
                file.elements.editor.value = content;
              }
            }
          }
        );
      }
      // Stop any running animation before starting new execution
      if (typeof __robotModule.__stopAnimation === 'function') {
        __robotModule.__stopAnimation();
      }
      if (typeof __robotModule.__initRobotField === 'function') {
        __robotModule.__initRobotField();
      }
    }
    let out = '';
  if (instance && instance.exports) {
      // Call global constructors if present (init_array handlers)
      if (typeof instance.exports.__wasm_call_ctors === 'function') {
        instance.exports.__wasm_call_ctors();
      }
      const entries = Object.entries(instance.exports)
        .filter(([k, v]) => typeof v === 'function' && !k.startsWith('__') && k !== '$$module_constructor' && k !== '$$module_destructor');
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
          // Bind string runtime to result runtime so that 'алг лит' values
          // (negative handles or C-string pointers) are interpreted via string.js.
          if (typeof resultEnv.setStringRuntime === 'function') {
            resultEnv.setStringRuntime(stringEnv);
          }
          const retType = resultEnv.wasmReturnType(bytes, name);
          const normalized = resultEnv.normalizeReturnValue(res, {
            returnType: retType,
            algType
          });
          out += `${name} => ${normalized}\n`;
          out += `time: ${micros} µs\n`;
        }
      } else {
        out += 'no exported functions to invoke\n';
      }
      // Call global destructors if present
      if (typeof instance.exports.__wasm_call_dtors === 'function') {
        instance.exports.__wasm_call_dtors();
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
    // Update robot field display after execution with animation
    if (__compilerOutputMode === 'robot' && __robotModule) {
      // Set render callback for animation
      if (typeof __robotModule.__setRenderCallback === 'function') {
        __robotModule.__setRenderCallback(renderRobotField);
      }
      // Check animation settings from UI
      const animEnabled = getCookie('q_robot_anim') !== '0';

      // Check if there's history to animate
      if (animEnabled && typeof __robotModule.__hasHistory === 'function' && __robotModule.__hasHistory() &&
          typeof __robotModule.__getHistoryLength === 'function' && __robotModule.__getHistoryLength() > 1) {
        // Apply animation speed
        const speedVal = getCookie('q_robot_speed');
        if (typeof __robotModule.__setAnimationDelay === 'function') {
          __robotModule.__setAnimationDelay(300 - parseInt(speedVal || '150', 10));
        }
        // Replay with animation - error will be shown after animation completes
        __robotModule.__replayHistory((deferredError) => {
          // Animation complete - show deferred error if any
          if (deferredError) {
            stdoutEl.textContent = deferredError;
            stdoutEl.classList.add('error');
          }
        });
      } else {
        // No animation - just render final state
        renderRobotField();
      }
    }
  } catch (e) {
    // For robot errors, don't show immediately - let animation play first
    if (__compilerOutputMode === 'robot' && __robotModule) {
      // Check animation settings
      const animEnabled = getCookie('q_robot_anim') !== '0';
      const hasHistory = typeof __robotModule.__hasHistory === 'function' && __robotModule.__hasHistory();
      const historyLen = typeof __robotModule.__getHistoryLength === 'function' ? __robotModule.__getHistoryLength() : 0;

      if (animEnabled && hasHistory && historyLen > 1) {
        // Set render callback and replay with animation
        if (typeof __robotModule.__setRenderCallback === 'function') {
          __robotModule.__setRenderCallback(renderRobotField);
        }
        // Apply animation speed
        const speedVal = getCookie('q_robot_speed');
        if (typeof __robotModule.__setAnimationDelay === 'function') {
          __robotModule.__setAnimationDelay(300 - parseInt(speedVal || '150', 10));
        }
        const stdoutEl = $('#stdout');
        stdoutEl.textContent = ''; // Clear while animating
        __robotModule.__replayHistory((deferredError) => {
          // Animation complete - show the error
          stdoutEl.textContent = deferredError || e.message;
          stdoutEl.classList.add('error');
        });
      } else {
        // No animation - show error immediately and render final state
        $('#stdout').textContent = e.message;
        $('#stdout').classList.add('error');
        renderRobotField();
      }
    } else {
      $('#stdout').textContent = e.message;
      $('#stdout').classList.add('error');
    }
  }
}
function loadState() {
  bootstrapProjects();
  const active = getActiveProject();
  if (active) {
    applyProjectToInputs(active, { silent: true });
  } else {
    const c = readPersistedValue('q_code');
    setCode((c !== null && c !== undefined) ? c : sample);
    const a = readPersistedValue('q_args');
    if (a !== null && a !== undefined) $('#args').value = a;
  }
  const v = readPersistedValue('q_view');
  if (v !== null && v !== undefined) $('#view').value = v;
  const o = readPersistedValue('q_opt');
  if (o !== null && o !== undefined) $('#opt').value = o;
  const pane = getCookie(IO_PANE_COOKIE);
  if (pane) __currentIoPane = pane;
}

function saveState() {
  updateActiveProjectFromInputs();
  writePersistedValue('q_code', getCode());
  writePersistedValue('q_args', $('#args').value || '');
  writePersistedValue('q_view', $('#view').value || 'ir');
  writePersistedValue('q_opt', $('#opt').value || '0');
}

// Initialize CodeMirror if available
function initEditor() {
  const ta = document.getElementById('code');
  if (!ta) return;
  if (typeof window.CodeMirror === 'undefined') {
    ta.addEventListener('input', () => { saveState(); debounceShow(); });
    return;
  }
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
      'Ctrl-/': cm => cm.execCommand('toggleComment'),
      'Ctrl-Enter': async () => {
        const runBtn = document.getElementById('btn-run');
        if (runBtn) runBtn.click();
      }
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

initIoWorkspace();  // Must be before loadState() so __ioFilesRoot is ready
loadState();
initProjectsUI();
// Load examples list
(async function initExamples(){
  try {
    const data = await apiGet('/api/examples');
    const sel = $('#examples');
    if (sel && data && Array.isArray(data.examples)) {
      // Fill options grouped by folder prefix
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
// If URL has ?share=<id>, load the shared snippet (code, args, stdin, files, ...) as a dedicated project.
// The project name is "Проект (открыт из ссылки <id>)". If a project with that
// name already exists, its contents are overwritten instead of creating a new one.
(async function loadSharedFromQuery(){
  try {
    const params = new URLSearchParams(window.location.search);
    const sid = params.get('share');
    if (!sid) return;
    const data = await apiGet('/api/share?id=' + encodeURIComponent(sid));

    // Normalize payload into a project-like shape
    let code = '';
    let args = '';
    let stdin = '';
    if (typeof data === 'string') {
      code = data;
    } else if (data && typeof data === 'object') {
      if (typeof data.code === 'string') code = data.code;
      if (typeof data.args === 'string') args = data.args;
      if (typeof data.stdin === 'string') stdin = data.stdin;
    }

  // Derive project name and either reuse existing project with that name
  // or create a new one. We don't include a numeric suffix here so that
  // repeated opens of the same link don't create extra projects.
    const displayName = `Проект (открыт из ссылки ${sid})`;
    let project = __projects.find(p => p.name === displayName);
    if (!project) {
      project = createProject({ name: displayName, code, args, stdin }, { activate: true });
    } else {
      project.code = code;
      project.args = args;
      project.stdin = stdin;
      project.updatedAt = Date.now();
      persistProjects();
      scheduleProjectsRender();
      setActiveProject(project.id, { silent: true });
      applyProjectToInputs(project, { silent: true });
    }

    const stdinEl = $('#stdin');
    if (stdinEl) {
      stdinEl.value = stdin;
    }

    // Restore IO files from shared payload (object name -> content).
    const nextFiles = [];
    if (data && typeof data === 'object' && data.files && typeof data.files === 'object') {
      let idx = 0;
      for (const [name, content] of Object.entries(data.files)) {
        if (typeof content !== 'string') continue;
        nextFiles.push(normalizeIoFile({ name, content }, idx++));
      }
    }

    if (__ioFilesRoot) {
      __ioFilesRoot.innerHTML = '';
    }
    __ioFiles.length = 0;
    nextFiles.forEach(file => {
      __ioFiles.push(file);
      renderIoFilePane(file);
    });
    refreshIoSelectOptions();

    debounceShow();
    const statusEl = document.getElementById('status');
    if (statusEl) statusEl.textContent = `Загружено из ссылки: ${sid}`;
  } catch (e) {
    console.warn('failed to load share:', e);
  }
})();

// If URL has ?example=<path>, load that example directly
(async function loadExampleFromQuery(){
  try {
    const params = new URLSearchParams(window.location.search);
    const examplePath = params.get('example');
    if (!examplePath) return;

    const data = await apiGet('/api/example?path=' + encodeURIComponent(examplePath));
    const displayName = deriveExampleProjectName(examplePath);
    const code = data.code || '';
    const args = data.args || '';

    // Prepare files array for the project
    const projectFiles = [];
    if (Array.isArray(data.files) && data.files.length > 0) {
      for (const f of data.files) {
        projectFiles.push({ name: f.name, content: f.content || '' });
      }
    }

    updateActiveProjectFromInputs();
    createProject({ name: displayName, code: code, args: args, stdin: '', files: projectFiles }, { activate: true });

    // Select example in dropdown if present
    const examplesSel = $('#examples');
    if (examplesSel) {
      examplesSel.value = examplePath;
    }

    debounceShow();
    const statusEl = document.getElementById('status');
    if (statusEl) statusEl.textContent = `Пример: ${examplePath}`;
  } catch (e) {
    console.warn('failed to load example from query:', e);
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

// Best-effort save on page unload to avoid losing recent edits
if (typeof window !== 'undefined') {
  window.addEventListener('beforeunload', () => {
    try { saveState(); } catch (_) {}
  });
}

// Auto-load example when selection changes
const examplesSel = $('#examples');
if (examplesSel) examplesSel.addEventListener('change', async () => {
  const path = examplesSel.value || '';
  if (!path) return;
  try {
    // Load example (code + optional metadata + files)
    const data = await apiGet('/api/example?path=' + encodeURIComponent(path));
    const displayName = deriveExampleProjectName(path);

    const code = data.code || '';
    const args = data.args || '';

    // Prepare files array for the project
    const projectFiles = [];
    if (Array.isArray(data.files) && data.files.length > 0) {
      for (const f of data.files) {
        projectFiles.push({ name: f.name, content: f.content || '' });
      }
    }

    updateActiveProjectFromInputs();
    createProject({ name: displayName, code: code, args: args, stdin: '', files: projectFiles }, { activate: true });

    debounceShow();
  } catch (e) {
    alert('Не удалось загрузить пример: ' + (e.message || String(e)));
  } finally {
    examplesSel.value = '';
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
  | табличный тип: цел таб A[0:9]`
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
  `Вставить: тип/объявление\n\nцел x, y\n| табличный тип: цел таб A[0:n]`
  };
  ['while','for','if','switch','func','decl'].forEach(k => {
    const btn = document.getElementById(`btn-snippet-${k}`);
    if (btn) btn.setAttribute('data-tooltip', preview[k]);
  });
}

// JS-driven tooltip (more reliable across browsers)
(() => {
  let tipEl = null;
  function showTip(target, { placeAbove = false } = {}) {
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
    let top;
    if (placeAbove) {
      // For compact footer icons: tooltip above the element
      top = r.top - tipEl.offsetHeight - pad;
      top = Math.max(4, top);
    } else {
      // Default behavior: tooltip below the element
      top = r.bottom + pad;
    }
    const left = Math.max(8, Math.min(window.innerWidth - tipEl.offsetWidth - 8, r.left + r.width / 2 - (tipEl.offsetWidth / 2)));
    tipEl.style.top = `${top}px`;
    tipEl.style.left = `${left}px`;
  }
  function hideTip() {
    if (tipEl) tipEl.style.display = 'none';
  }
  // Attach default (below) tooltips to snippet buttons
  ['while','for','if','switch','func','decl'].forEach(k => {
    const btn = document.getElementById(`btn-snippet-${k}`);
    if (!btn) return;
    btn.addEventListener('mouseenter', () => showTip(btn));
    btn.addEventListener('mouseleave', hideTip);
    btn.addEventListener('focus', () => showTip(btn));
    btn.addEventListener('blur', hideTip);
  });
  // Attach "above" tooltip only to the bug icon link in the footer
  const bugLink = document.querySelector('footer a[data-tooltip]');
  const bugBtn = document.getElementById('bug-report-btn');
  const bugTarget = bugBtn || bugLink;
  if (bugTarget) {
    bugTarget.addEventListener('mouseenter', () => showTip(bugTarget, { placeAbove: true }));
    bugTarget.addEventListener('mouseleave', hideTip);
    bugTarget.addEventListener('focus', () => showTip(bugTarget, { placeAbove: true }));
    bugTarget.addEventListener('blur', hideTip);
  }
  // Attach "above" tooltips to docs and tour buttons in footer
  ['docs-page-btn', 'tour-restart-btn'].forEach(id => {
    const btn = document.getElementById(id);
    if (!btn) return;
    btn.addEventListener('mouseenter', () => showTip(btn, { placeAbove: true }));
    btn.addEventListener('mouseleave', hideTip);
    btn.addEventListener('focus', () => showTip(btn, { placeAbove: true }));
    btn.addEventListener('blur', hideTip);
  });
  // Attach default (below) tooltips to header controls
  ['opt', 'examples', 'view', 'btn-share', 'btn-help'].forEach(id => {
    const el = document.getElementById(id);
    if (!el) return;
    el.addEventListener('mouseenter', () => showTip(el));
    el.addEventListener('mouseleave', hideTip);
    el.addEventListener('focus', () => showTip(el));
    el.addEventListener('blur', hideTip);
  });
  // Attach default (below) tooltips to IO toolbar controls
  ['io-select', 'io-add-file', 'args', 'btn-run'].forEach(id => {
    const el = document.getElementById(id);
    if (!el) return;
    el.addEventListener('mouseenter', () => showTip(el));
    el.addEventListener('mouseleave', hideTip);
    el.addEventListener('focus', () => showTip(el));
    el.addEventListener('blur', hideTip);
  });
  // Attach "above" tooltip to fullscreen close button
  const fsClose = document.getElementById('fs-close');
  if (fsClose) {
    fsClose.addEventListener('mouseenter', () => showTip(fsClose, { placeAbove: true }));
    fsClose.addEventListener('mouseleave', hideTip);
    fsClose.addEventListener('focus', () => showTip(fsClose, { placeAbove: true }));
    fsClose.addEventListener('blur', hideTip);
  }
})();

// Bug report button copies diagnostics to clipboard and opens GitHub issues
(() => {
  const btn = document.getElementById('bug-report-btn');
  if (!btn) return;
  const baseUrl = 'https://github.com/resetius/qumir/issues/new';

  function collectDiagnostics() {
    const code = getCode();
    const args = ($('#args') && $('#args').value) || '';
    const stdin = ($('#stdin') && $('#stdin').value) || '';
    const stdout = ($('#stdout') && $('#stdout').textContent) || '';
    const view = ($('#view') && $('#view').value) || '';
    const opt = ($('#opt') && $('#opt').value) || '';
    const revisionEl = document.getElementById('version');
    const revision = revisionEl ? revisionEl.textContent.trim() : '';
    let files = [];
    try {
      const active = getActiveProject();
      if (active && Array.isArray(active.files)) {
        files = active.files.map(f => ({ name: f.name, content: f.content }));
      }
    } catch (_) {}

    const lines = [];
    lines.push('### Программа');
    lines.push('```kumir');
    lines.push(code);
    lines.push('```');
    lines.push('');
    lines.push('### Входные данные (stdin)');
    lines.push('```');
    lines.push(stdin);
    lines.push('```');
    lines.push('');
    lines.push('### Аргументы запуска');
    lines.push(args || '—');
    lines.push('');
    lines.push('### Файлы');
    if (files.length === 0) {
      lines.push('нет файлов');
    } else {
      files.forEach(f => {
        lines.push('#### ' + f.name);
        lines.push('```');
        lines.push(f.content || '');
        lines.push('```');
      });
    }
    lines.push('');
    lines.push('### Вывод компилятора / программы');
    lines.push('```');
    lines.push(stdout);
    lines.push('```');
    lines.push('');
    lines.push('### Среда');
    lines.push(`view: ${view || '—'}, opt: ${opt || '—'}`);
    if (revision) lines.push(`version: ${revision}`);

    return lines.join('\n');
  }

  async function copyDiagnostics() {
    const text = collectDiagnostics();
    try {
      if (navigator.clipboard && navigator.clipboard.writeText) {
        await navigator.clipboard.writeText(text);
      } else {
        const ta = document.createElement('textarea');
        ta.value = text;
        ta.style.position = 'fixed';
        ta.style.opacity = '0';
        document.body.appendChild(ta);
        ta.select();
        document.execCommand('copy');
        document.body.removeChild(ta);
      }
      showToast('Диагностика скопирована в буфер обмена');
    } catch (_) {
      showToast('Не удалось скопировать диагностику');
    }
  }

  btn.addEventListener('click', async () => {
    await copyDiagnostics();
    const title = 'Баг в Qumir Playground';
    const shortBody = [
      'Опишите, пожалуйста, что вы ожидали и что произошло. ',
      'Полная диагностика (код, stdin, файлы, вывод) уже скопирована в буфер обмена — ',
      'просто вставьте её сюда (Cmd+V / Ctrl+V).',
      '',
      '---'
    ].join('\n');
    const url = `${baseUrl}?title=${encodeURIComponent(title)}&body=${encodeURIComponent(shortBody)}`;
    try { window.open(url, '_blank', 'noopener,noreferrer'); } catch (_) {
      window.location.href = url;
    }
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
  const compOutEl = document.getElementById('output');
  const titleOutput = document.getElementById('title-output');
  if (compOutEl && titleOutput) {
    titleOutput.addEventListener('mousedown', e => e.preventDefault());
    titleOutput.addEventListener('click', () => {
      const node = getCurrentCompilerOutputNode();
      open('Compiler output', node);
    });
  }
  const titleIo = document.getElementById('title-io');
  if (titleIo) {
    titleIo.addEventListener('mousedown', e => e.preventDefault());
    titleIo.addEventListener('click', () => {
      const node = getCurrentIoPaneNode();
      if (node) open(`IO • ${getCurrentIoPaneLabel()}`, node);
    });
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
  // Collect IO files into a simple name->content map
  const files = {};
  for (const file of __ioFiles) {
    const name = canonicalIoFileName(file.name || '');
    const key = name || file.id;
    if (!key) continue;
    files[key] = file.content || '';
  }
    try {
      const r = await fetch('/api/share', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ code, args, stdin, files })
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

// Onboarding tour
(async function setupTour() {
  try {
    const tour = await import('./tour.js');

    // Button to restart tour
    const restartBtn = document.getElementById('tour-restart-btn');
    if (restartBtn) {
      restartBtn.addEventListener('click', () => {
        tour.resetTour();
        tour.startTour();
      });
    }

    // Don't auto-start tour if coming from a shared link or example
    const params = new URLSearchParams(window.location.search);
    if (!params.get('share') && !params.get('example')) {
      tour.initTour();
    }
  } catch (e) {
    console.warn('Tour module not available:', e);
  }
})();

// Documentation panel
initDocs();
