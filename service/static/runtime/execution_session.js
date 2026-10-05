export function supportsJspi() {
  return typeof WebAssembly.Suspending === 'function'
    && typeof WebAssembly.promising === 'function';
}

export class ExecutionStopped extends Error {
  constructor() { super('Остановлено'); }
}

export class ExecutionSession {
  constructor({ debugger: debuggerInstance = null, render = () => {}, delay = () => 0 } = {}) {
    this.debugger = debuggerInstance;
    this.render = render;
    this.delay = delay;
    this.controller = new AbortController();
    this.lastYield = performance.now();
    this.lastRender = this.lastYield;
    this.checks = 0;
    this.instance = null;
  }

  check() {
    if (this.controller.signal.aborted) throw this.controller.signal.reason;
  }

  wait(promise) {
    const signal = this.controller.signal;
    this.check();
    return new Promise((resolve, reject) => {
      const abort = () => {
        signal.removeEventListener('abort', abort);
        reject(signal.reason);
      };
      signal.addEventListener('abort', abort, { once: true });
      Promise.resolve(promise).then(resolve, reject).finally(() => {
        signal.removeEventListener('abort', abort);
      });
    });
  }

  sleep(milliseconds) {
    const signal = this.controller.signal;
    this.check();
    return new Promise((resolve, reject) => {
      const abort = () => { clearTimeout(timer); reject(signal.reason); };
      const timer = setTimeout(() => {
        signal.removeEventListener('abort', abort);
        resolve();
      }, milliseconds);
      signal.addEventListener('abort', abort, { once: true });
    });
  }

  operation(execute) {
    this.check();
    const result = execute();
    const finish = value => {
      this.check();
      const delay = this.delay();
      const now = performance.now();
      if (delay > 0 || now - this.lastRender >= 1000) {
        this.render();
        this.lastRender = now;
        return this.sleep(delay).then(() => value);
      }
      return value;
    };
    if (result instanceof Promise) {
      this.render();
      return this.wait(result).then(finish);
    }
    return finish(result);
  }

  checkpoint(id, address) {
    this.check();
    const paused = this.debugger?.point(id, address);
    if (paused instanceof Promise) return this.wait(paused);
    if (++this.checks >= 512) {
      this.checks = 0;
      const now = performance.now();
      if (now - this.lastYield >= 16) {
        this.lastYield = now;
        return this.sleep(0);
      }
    }
    return undefined;
  }

  suspending(callback) {
    return new WebAssembly.Suspending((...args) => {
      this.check();
      const result = callback(...args);
      return result instanceof Promise ? this.wait(result) : result;
    });
  }

  async call(fn, args = []) {
    this.check();
    return await WebAssembly.promising(fn)(...args);
  }

  stop() {
    if (!this.controller.signal.aborted) this.controller.abort(new ExecutionStopped());
    this.debugger?.finish();
    this.instance = null;
  }

  finish() {
    if (!this.controller.signal.aborted) this.controller.abort(new ExecutionStopped());
    this.debugger?.finish();
    this.render();
    this.instance = null;
  }
}

export function bindJspiImports(env, imports, runtimeInfo, session, debugData = null) {
  for (const name of runtimeInfo?.asyncImports || []) {
    if (!imports.some(item => item.module === 'env' && item.name === name)) continue;
    const fn = env[name + '_jspi'];
    if (typeof fn !== 'function') throw new Error(`Нет JSPI runtime для ${name}`);
    env[name] = session.suspending((...args) => session.operation(() => fn(...args)));
  }
  // Sleep belongs to the session so Stop cancels its timer as well.
  if (imports.some(item => item.name === 'qumir_sleep') && runtimeInfo?.mode === 'jspi') {
    env.qumir_sleep = session.suspending(milliseconds => session.sleep(Math.max(0, Number(milliseconds) || 0)));
  }
  if (debugData) env.__qumir_debug_point = session.suspending((id, address) => session.checkpoint(id, address));
}
