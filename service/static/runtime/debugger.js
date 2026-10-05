export class Debugger {
  constructor({ breakpoints = new Set(), onChange = () => {} } = {}) {
    this.breakpoints = breakpoints;
    this.onChange = onChange;
    this.data = null;
    this.frames = [];
    this.nextFrame = 0;
    this.mode = 'in';
    this.target = null;
    this.paused = false;
    this.pauseRequested = false;
    this.resumeCallback = null;
  }

  point(id, address) {
    const point = this.data?.points[id];
    if (!point) return;
    if (point.kind === 'enter') {
      this.frames.push({ id: ++this.nextFrame, function: point.function, point, locals: new Map() });
      return;
    }
    const frame = this.frames.at(-1);
    if (!frame || frame.function !== point.function) return;
    if (point.kind === 'variable') {
      frame.locals.set(point.local, Number(address) >>> 0);
      return;
    }
    if (point.kind === 'leave') {
      this.frames.pop();
      return;
    }
    frame.point = point;
    const statement = point.kind === 'statement' && point.line > 0;
    const returned = this.target !== null && !this.frames.some(f => f.id === this.target);
    const step = (statement && (this.mode === 'in' || (this.mode === 'over' && frame.id === this.target)))
      || ((this.mode === 'out' || this.mode === 'over') && returned);
    if (!this.pauseRequested && !step && !(statement && this.breakpoints.has(point.line))) return;
    this.pauseRequested = false;
    this.paused = true;
    // The callback may resume immediately (e.g. a headless debugger client).
    return new Promise(resolve => {
      this.resumeCallback = resolve;
      this.onChange(this);
    });
  }

  resume(mode = 'continue', frame = this.frames.at(-1)) {
    if (!this.paused) return;
    this.mode = mode;
    this.target = frame?.id ?? null;
    this.paused = false;
    const resume = this.resumeCallback;
    this.resumeCallback = null;
    this.onChange(this);
    resume?.();
  }

  pause() { this.pauseRequested = true; }

  visibleLocals(frame) {
    if (!frame) return [];
    const fun = this.data.functions[frame.function];
    const scopes = new Set();
    let scope = frame.point.scope;
    while (scope >= 0 && !scopes.has(scope)) {
      scopes.add(scope);
      scope = fun.parents[scope] ?? -1;
    }
    const names = new Set();
    const result = [];
    // Inner scopes win when source names are shadowed.
    for (const scopeId of scopes) {
      for (const [id, address] of frame.locals) {
        const local = fun.locals[id];
        if (!local || local.scope !== scopeId || !local.name || local.name.startsWith('$') || names.has(local.name)) continue;
        names.add(local.name);
        result.push({ ...local, id, address });
      }
    }
    return result;
  }

  binding(frame, line, column) {
    const binding = this.data.bindings.find(b => b.function === frame.function && b.line === line
      && column >= b.column && column < b.column + [...b.name].length);
    return binding && this.visibleLocals(frame).find(local => local.id === binding.local);
  }

  finish() {
    this.paused = false;
    this.frames = [];
    this.resumeCallback?.();
    this.resumeCallback = null;
    this.onChange(this);
  }
}
