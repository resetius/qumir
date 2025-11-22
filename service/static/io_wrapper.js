'use strict';

// Helper utilities to adapt DOM elements (#stdin/#stdout) into the runtime IO
// stream API. Keeps the core IO runtime free of browser-specific references.

function resolveElement(target, fallbackSelector) {
  if (!target && typeof document !== 'undefined') {
    return document.querySelector(fallbackSelector);
  }
  if (typeof target === 'string' && typeof document !== 'undefined') {
    return document.querySelector(target);
  }
  return target || null;
}

function createTokenizingInputStream(resolveFn) {
  let tokens = [];
  let index = 0;
  const reload = () => {
    const el = resolveFn();
    const raw = el ? String(el.value || '') : '';
    tokens = raw.match(/\S+/g) || [];
    index = 0;
  };
  reload();
  return {
    readToken() {
      if (index < tokens.length) return tokens[index++];
      return '0';
    },
    reset() {
      reload();
    }
  };
}

function createTextContentOutputStream(resolveFn) {
  return {
    write(str) {
      const el = resolveFn();
      if (!el) return;
      el.textContent += String(str);
    },
    clear() {
      const el = resolveFn();
      if (!el) return;
      el.textContent = '';
      el.classList.remove('error');
    }
  };
}

export function bindBrowserIO(ioRuntime, options = {}) {
  if (!ioRuntime || typeof ioRuntime.setInputStream !== 'function' || typeof ioRuntime.setOutputStream !== 'function') {
    throw new Error('ioRuntime must expose setInputStream/setOutputStream.');
  }
  const {
    stdin: stdinTarget = '#stdin',
    stdout: stdoutTarget = '#stdout'
  } = options;
  const stdinEl = resolveElement(stdinTarget, '#stdin');
  const stdoutEl = resolveElement(stdoutTarget, '#stdout');
  if (!stdinEl) throw new Error('STDIN element not found.');
  if (!stdoutEl) throw new Error('STDOUT element not found.');

  const inputStream = createTokenizingInputStream(() => stdinEl);
  const outputStream = createTextContentOutputStream(() => stdoutEl);
  ioRuntime.setInputStream(inputStream);
  ioRuntime.setOutputStream(outputStream);
  return { inputStream, outputStream, stdinEl, stdoutEl };
}
