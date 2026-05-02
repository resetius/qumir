// Painter (Рисователь) runtime for Qumir WebAssembly
// Implements all painter_* host functions as WASM imports.
// Helpers exported for app.js:
//   __bindMemory(mem)
//   __bindPainterCanvas(canvas)
//   __bindStringRuntime(stringEnv)
//   __resetPainter()
//   __onCanvasShown()

let MEMORY = null;
let stringRuntime = null;

// Offscreen buffer — all drawing goes here
let offscreen = null;
let offCtx = null;

// Visible canvas
let canvas = null;
let ctx = null;
let dpr = 1;

let rafScheduled = false;

const state = {
  sheetW: 800, sheetH: 600,
  penWidth: 1,
  penColor: 0xFF000000n,
  brushColor: 0xFFFFFFFFn,
  hasBrush: true,
  density: 100,
  fontFamily: 'Arial',
  fontSize: 12,
  fontBold: false,
  fontItalic: false,
  curX: 0, curY: 0,
};

// ── Helpers ───────────────────────────────────────────────────────────────────

function argbToStyle(color) {
  const c = BigInt.asUintN(32, BigInt(color));
  const a = Number((c >> 24n) & 0xFFn);
  const r = Number((c >> 16n) & 0xFFn);
  const g = Number((c >>  8n) & 0xFFn);
  const b = Number( c         & 0xFFn);
  return `rgba(${r},${g},${b},${(a / 255).toFixed(6)})`;
}

function wasmAddr(ptr) {
  return Number(BigInt.asUintN(32, BigInt(ptr)));
}

function readCString(ptr) {
  if (!MEMORY) return '';
  if (stringRuntime && typeof stringRuntime.__loadString === 'function') {
    return stringRuntime.__loadString(ptr);
  }
  const addr = typeof ptr === 'bigint' ? wasmAddr(ptr) : Number(ptr);
  const bytes = new Uint8Array(MEMORY.buffer, addr);
  let end = 0;
  while (end < bytes.length && bytes[end] !== 0) end++;
  return new TextDecoder().decode(bytes.subarray(0, end));
}

function writeI64(ptr, value) {
  if (!MEMORY) return;
  new DataView(MEMORY.buffer).setBigInt64(wasmAddr(ptr), BigInt(value), true);
}

function readI64Array(ptr, n) {
  if (!MEMORY) return [];
  const base = wasmAddr(ptr);
  const dv = new DataView(MEMORY.buffer);
  const out = [];
  for (let i = 0; i < n; i++) out.push(Number(dv.getBigInt64(base + i * 8, true)));
  return out;
}

function makeFont() {
  const style  = state.fontItalic ? 'italic ' : '';
  const weight = state.fontBold   ? 'bold '   : '';
  return `${style}${weight}${state.fontSize}px ${state.fontFamily}`;
}

function applyStroke() {
  if (!offCtx) return;
  offCtx.strokeStyle = argbToStyle(state.penColor);
  offCtx.lineWidth   = Number(state.penWidth);
}

function applyFill() {
  if (!offCtx) return;
  offCtx.fillStyle = state.hasBrush ? argbToStyle(state.brushColor) : 'rgba(0,0,0,0)';
}

function scheduleDraw() {
  if (rafScheduled || !ctx || !canvas || !offscreen) return;
  rafScheduled = true;
  requestAnimationFrame(() => {
    rafScheduled = false;
    draw();
  });
}

function draw() {
  if (!ctx || !canvas || !offscreen) return;
  fitCanvas();
  const cssW = canvas.width / dpr;
  const cssH = canvas.height / dpr;
  ctx.clearRect(0, 0, cssW, cssH);
  // Fit sheet into canvas preserving aspect ratio
  const scale = Math.min(cssW / state.sheetW, cssH / state.sheetH);
  const dw = state.sheetW * scale;
  const dh = state.sheetH * scale;
  const ox = (cssW - dw) / 2;
  const oy = (cssH - dh) / 2;
  ctx.drawImage(offscreen, ox, oy, dw, dh);
}

function fitCanvas() {
  if (!canvas) return;
  const rect = canvas.getBoundingClientRect();
  dpr = window.devicePixelRatio || 1;
  const w = Math.max(1, Math.floor(rect.width));
  const h = Math.max(1, Math.floor(rect.height));
  if (canvas.width !== Math.floor(w * dpr) || canvas.height !== Math.floor(h * dpr)) {
    canvas.width  = Math.floor(w * dpr);
    canvas.height = Math.floor(h * dpr);
    if (ctx) ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  }
}

function makeOffscreen(w, h) {
  if (typeof OffscreenCanvas !== 'undefined') return new OffscreenCanvas(w, h);
  return null;
}

// ── Scanline flood fill ───────────────────────────────────────────────────────

function floodFill(imageData, startX, startY, fr, fg, fb, fa) {
  const { data, width: W, height: H } = imageData;
  const si = (startY * W + startX) * 4;
  const tr = data[si], tg = data[si+1], tb = data[si+2], ta = data[si+3];
  if (tr === fr && tg === fg && tb === fb && ta === fa) return;

  const matches = (x, y) => {
    const i = (y * W + x) * 4;
    return data[i]===tr && data[i+1]===tg && data[i+2]===tb && data[i+3]===ta;
  };
  const fillSpan = (xl, xr, y) => {
    let i = (y * W + xl) * 4;
    for (let x = xl; x <= xr; x++, i += 4) {
      data[i]=fr; data[i+1]=fg; data[i+2]=fb; data[i+3]=fa;
    }
  };

  // Find initial span
  let xl0 = startX, xr0 = startX;
  while (xl0 > 0   && matches(xl0 - 1, startY)) xl0--;
  while (xr0 < W-1 && matches(xr0 + 1, startY)) xr0++;
  fillSpan(xl0, xr0, startY);

  // Stack: [xl, xr, y, dir]  dir = ±1 (direction to continue scanning)
  const stack = [];
  if (startY > 0)   stack.push([xl0, xr0, startY - 1, -1]);
  if (startY < H-1) stack.push([xl0, xr0, startY + 1,  1]);

  while (stack.length > 0) {
    const [lx, rx, y, dir] = stack.pop();
    if (y < 0 || y >= H) continue;

    // Walk across [lx, rx], find and fill all matching spans
    let x = lx;
    while (x <= rx) {
      while (x <= rx && !matches(x, y)) x++;
      if (x > rx) break;

      // Extend span to the left (may go beyond lx)
      let spanL = x;
      while (spanL > 0   && matches(spanL - 1, y)) spanL--;
      // Extend span to the right
      let spanR = x;
      while (spanR < W-1 && matches(spanR + 1, y)) spanR++;

      fillSpan(spanL, spanR, y);

      // Continue in same direction
      const nextY = y + dir;
      if (nextY >= 0 && nextY < H) stack.push([spanL, spanR, nextY, dir]);

      // Extensions beyond [lx,rx] need a reverse scan
      const prevY = y - dir;
      if (prevY >= 0 && prevY < H) {
        if (spanL < lx) stack.push([spanL, lx - 1, prevY, -dir]);
        if (spanR > rx) stack.push([rx + 1, spanR, prevY, -dir]);
      }

      x = spanR + 1;
    }
  }
}

// ── Public helpers ────────────────────────────────────────────────────────────

export function __bindMemory(mem)          { MEMORY = mem; }
export function __bindStringRuntime(env)   { stringRuntime = env; }

export function __bindPainterCanvas(cnv) {
  canvas = cnv || null;
  ctx    = canvas ? canvas.getContext('2d') : null;
  if (canvas && ctx) {
    fitCanvas();
    scheduleDraw();
  }
}

export function __resetPainter() {
  state.sheetW = 800; state.sheetH = 600;
  state.penWidth = 1; state.penColor = 0xFF000000n;
  state.brushColor = 0xFFFFFFFFn; state.hasBrush = true;
  state.density = 100;
  state.fontFamily = 'Arial'; state.fontSize = 12;
  state.fontBold = false; state.fontItalic = false;
  state.curX = 0; state.curY = 0;
  offscreen = makeOffscreen(state.sheetW, state.sheetH);
  offCtx = offscreen ? offscreen.getContext('2d') : null;
  if (offCtx) {
    offCtx.fillStyle = '#ffffff';
    offCtx.fillRect(0, 0, state.sheetW, state.sheetH);
  }
  scheduleDraw();
}

export function __onCanvasShown() {
  if (!canvas) return;
  fitCanvas();
  scheduleDraw();
}

// ── Color constants ───────────────────────────────────────────────────────────

export function painter_transparent() { return 0x00000000n; }
export function painter_white()       { return 0xFFFFFFFFn; }
export function painter_black()       { return 0xFF000000n; }
export function painter_gray()        { return 0xFF808080n; }
export function painter_purple()      { return 0xFF800080n; }
export function painter_blue()        { return 0xFF0000FFn; }
export function painter_cyan()        { return 0xFF00FFFFn; }
export function painter_green()       { return 0xFF008000n; }
export function painter_yellow()      { return 0xFFFFFF00n; }
export function painter_orange()      { return 0xFFFFA500n; }
export function painter_red()         { return 0xFFFF0000n; }

// ── Color construction ────────────────────────────────────────────────────────

function packARGB(a, r, g, b) {
  return (BigInt(a & 0xFF) << 24n) | (BigInt(r & 0xFF) << 16n) |
         (BigInt(g & 0xFF) <<  8n) |  BigInt(b & 0xFF);
}

export function painter_rgb(r, g, b)       { return packARGB(255, Number(r), Number(g), Number(b)); }
export function painter_rgba(r, g, b, a)   { return packARGB(Number(a), Number(r), Number(g), Number(b)); }

function hueToRGB(p, q, t) {
  if (t < 0) t += 1; if (t > 1) t -= 1;
  if (t < 1/6) return p + (q - p) * 6 * t;
  if (t < 1/2) return q;
  if (t < 2/3) return p + (q - p) * (2/3 - t) * 6;
  return p;
}

function hslToRGB(h, s, l) {
  const hf = h/360, sf = s/100, lf = l/100;
  if (sf === 0) { const v = Math.round(lf*255); return [v,v,v]; }
  const q = lf < 0.5 ? lf*(1+sf) : lf+sf-lf*sf;
  const p = 2*lf - q;
  return [Math.round(hueToRGB(p,q,hf+1/3)*255), Math.round(hueToRGB(p,q,hf)*255), Math.round(hueToRGB(p,q,hf-1/3)*255)];
}

function hsvToRGB(h, s, v) {
  const hf = h/60, sf = s/100, vf = v/100;
  const i = Math.floor(hf) % 6;
  const f = hf - Math.floor(hf);
  const p = vf*(1-sf), q = vf*(1-f*sf), t = vf*(1-(1-f)*sf);
  const cases = [[vf,t,p],[q,vf,p],[p,vf,t],[p,q,vf],[t,p,vf],[vf,p,q]];
  const [r,g,b] = cases[i] || cases[0];
  return [Math.round(r*255), Math.round(g*255), Math.round(b*255)];
}

function cmykToRGB(c, m, y, k) {
  const cf=c/100, mf=m/100, yf=y/100, kf=k/100;
  return [Math.round((1-cf)*(1-kf)*255), Math.round((1-mf)*(1-kf)*255), Math.round((1-yf)*(1-kf)*255)];
}

export function painter_hsl(h, s, l)       { const [r,g,b] = hslToRGB(Number(h),Number(s),Number(l)); return packARGB(255,r,g,b); }
export function painter_hsla(h, s, l, a)   { const [r,g,b] = hslToRGB(Number(h),Number(s),Number(l)); return packARGB(Number(a),r,g,b); }
export function painter_hsv(h, s, v)       { const [r,g,b] = hsvToRGB(Number(h),Number(s),Number(v)); return packARGB(255,r,g,b); }
export function painter_hsva(h, s, v, a)   { const [r,g,b] = hsvToRGB(Number(h),Number(s),Number(v)); return packARGB(Number(a),r,g,b); }
export function painter_cmyk(c, m, y, k)   { const [r,g,b] = cmykToRGB(Number(c),Number(m),Number(y),Number(k)); return packARGB(255,r,g,b); }
export function painter_cmyka(c,m,y,k,a)   { const [r,g,b] = cmykToRGB(Number(c),Number(m),Number(y),Number(k)); return packARGB(Number(a),r,g,b); }

// ── Color decomposition ───────────────────────────────────────────────────────

export function painter_decompose_rgb(color, rPtr, gPtr, bPtr) {
  const c = BigInt.asUintN(32, BigInt(color));
  writeI64(rPtr, (c >> 16n) & 0xFFn);
  writeI64(gPtr, (c >>  8n) & 0xFFn);
  writeI64(bPtr,  c         & 0xFFn);
}

export function painter_decompose_cmyk(color, cPtr, mPtr, yPtr, kPtr) {
  const c = BigInt.asUintN(32, BigInt(color));
  const r = Number((c >> 16n) & 0xFFn) / 255;
  const g = Number((c >>  8n) & 0xFFn) / 255;
  const b = Number( c         & 0xFFn) / 255;
  const k = 1 - Math.max(r, g, b);
  if (k >= 1) { writeI64(cPtr,0); writeI64(mPtr,0); writeI64(yPtr,0); writeI64(kPtr,100); return; }
  writeI64(cPtr, Math.round((1-r-k)/(1-k)*100));
  writeI64(mPtr, Math.round((1-g-k)/(1-k)*100));
  writeI64(yPtr, Math.round((1-b-k)/(1-k)*100));
  writeI64(kPtr, Math.round(k*100));
}

export function painter_decompose_hsl(color, hPtr, sPtr, lPtr) {
  const c = BigInt.asUintN(32, BigInt(color));
  const r = Number((c >> 16n) & 0xFFn) / 255;
  const g = Number((c >>  8n) & 0xFFn) / 255;
  const b = Number( c         & 0xFFn) / 255;
  const mx = Math.max(r,g,b), mn = Math.min(r,g,b);
  const lf = (mx+mn)/2;
  let sf=0, hf=0;
  if (mx !== mn) {
    const d = mx-mn;
    sf = lf > 0.5 ? d/(2-mx-mn) : d/(mx+mn);
    if      (mx===r) hf = (g-b)/d + (g<b?6:0);
    else if (mx===g) hf = (b-r)/d + 2;
    else             hf = (r-g)/d + 4;
    hf /= 6;
  }
  writeI64(hPtr, Math.round(hf*360));
  writeI64(sPtr, Math.round(sf*100));
  writeI64(lPtr, Math.round(lf*100));
}

export function painter_decompose_hsv(color, hPtr, sPtr, vPtr) {
  const c = BigInt.asUintN(32, BigInt(color));
  const r = Number((c >> 16n) & 0xFFn) / 255;
  const g = Number((c >>  8n) & 0xFFn) / 255;
  const b = Number( c         & 0xFFn) / 255;
  const mx = Math.max(r,g,b), mn = Math.min(r,g,b), d = mx-mn;
  const sf = mx === 0 ? 0 : d/mx;
  let hf = 0;
  if (d !== 0) {
    if      (mx===r) hf = (g-b)/d + (g<b?6:0);
    else if (mx===g) hf = (b-r)/d + 2;
    else             hf = (r-g)/d + 4;
    hf /= 6;
  }
  writeI64(hPtr, Math.round(hf*360));
  writeI64(sPtr, Math.round(sf*100));
  writeI64(vPtr, Math.round(mx*100));
}

// ── Sheet info ────────────────────────────────────────────────────────────────

export function painter_sheet_width()  { return BigInt(state.sheetW); }
export function painter_sheet_height() { return BigInt(state.sheetH); }
export function painter_center_x()     { return BigInt(Math.floor(state.sheetW / 2)); }
export function painter_center_y()     { return BigInt(Math.floor(state.sheetH / 2)); }

export function painter_text_width(charPtr) {
  const text = readCString(charPtr);
  if (!offCtx) return BigInt(text.length * Math.floor(state.fontSize / 2));
  offCtx.save();
  offCtx.font = makeFont();
  const w = offCtx.measureText(text).width;
  offCtx.restore();
  return BigInt(Math.round(w));
}

export function painter_get_pixel(x, y) {
  if (!offCtx) return 0n;
  const px = Number(x), py = Number(y);
  if (px < 0 || py < 0 || px >= state.sheetW || py >= state.sheetH) return 0n;
  const d = offCtx.getImageData(px, py, 1, 1).data;
  return packARGB(d[3], d[0], d[1], d[2]);
}

// ── Drawing parameters ────────────────────────────────────────────────────────

export function painter_pen(width, color) {
  state.penWidth = Number(width);
  state.penColor = BigInt(color);
}

export function painter_brush(color) {
  state.brushColor = BigInt(color);
  state.hasBrush = true;
}

export function painter_no_brush() {
  state.hasBrush = false;
}

export function painter_density(d) {
  state.density = Number(d);
}

export function painter_font(familyPtr, size, bold, italic) {
  state.fontFamily = readCString(familyPtr) || 'Arial';
  state.fontSize   = Number(size);
  state.fontBold   = Boolean(bold);
  state.fontItalic = Boolean(italic);
}

// ── Drawing commands ──────────────────────────────────────────────────────────

export function painter_move_to(x, y) {
  state.curX = Number(x);
  state.curY = Number(y);
}

export function painter_line(x1, y1, x2, y2) {
  if (!offCtx) return;
  offCtx.save();
  applyStroke();
  offCtx.globalAlpha = state.density / 100;
  offCtx.beginPath();
  offCtx.moveTo(Number(x1), Number(y1));
  offCtx.lineTo(Number(x2), Number(y2));
  offCtx.stroke();
  offCtx.restore();
}

export function painter_line_to(x, y) {
  if (!offCtx) return;
  const nx = Number(x), ny = Number(y);
  offCtx.save();
  applyStroke();
  offCtx.globalAlpha = state.density / 100;
  offCtx.beginPath();
  offCtx.moveTo(state.curX, state.curY);
  offCtx.lineTo(nx, ny);
  offCtx.stroke();
  offCtx.restore();
  state.curX = nx; state.curY = ny;
}

export function painter_polygon(n, xsPtr, ysPtr) {
  if (!offCtx) return;
  const count = Number(n);
  if (count < 2) return;
  const xs = readI64Array(xsPtr, count);
  const ys = readI64Array(ysPtr, count);
  offCtx.save();
  applyStroke(); applyFill();
  offCtx.globalAlpha = state.density / 100;
  offCtx.beginPath();
  offCtx.moveTo(xs[0], ys[0]);
  for (let i = 1; i < count; i++) offCtx.lineTo(xs[i], ys[i]);
  offCtx.closePath();
  if (state.hasBrush) offCtx.fill();
  offCtx.stroke();
  offCtx.restore();
}

export function painter_pixel(x, y, color) {
  if (!offCtx) return;
  offCtx.fillStyle = argbToStyle(color);
  offCtx.fillRect(Number(x), Number(y), 1, 1);
}

export function painter_rect(x, y, w, h) {
  if (!offCtx) return;
  offCtx.save();
  applyStroke(); applyFill();
  offCtx.globalAlpha = state.density / 100;
  if (state.hasBrush) offCtx.fillRect(Number(x), Number(y), Number(w), Number(h));
  offCtx.strokeRect(Number(x), Number(y), Number(w), Number(h));
  offCtx.restore();
}

export function painter_ellipse(x, y, w, h) {
  if (!offCtx) return;
  const cx = Number(x) + Number(w)/2;
  const cy = Number(y) + Number(h)/2;
  offCtx.save();
  applyStroke(); applyFill();
  offCtx.globalAlpha = state.density / 100;
  offCtx.beginPath();
  offCtx.ellipse(cx, cy, Number(w)/2, Number(h)/2, 0, 0, Math.PI*2);
  if (state.hasBrush) offCtx.fill();
  offCtx.stroke();
  offCtx.restore();
}

export function painter_circle(x, y, r) {
  if (!offCtx) return;
  const radius = Number(r);
  offCtx.save();
  applyStroke(); applyFill();
  offCtx.globalAlpha = state.density / 100;
  offCtx.beginPath();
  offCtx.arc(Number(x), Number(y), radius, 0, Math.PI*2);
  if (state.hasBrush) offCtx.fill();
  offCtx.stroke();
  offCtx.restore();
}

export function painter_text(x, y, charPtr) {
  if (!offCtx) return;
  const text = readCString(charPtr);
  offCtx.save();
  offCtx.globalAlpha = state.density / 100;
  offCtx.font = makeFont();
  offCtx.fillStyle = argbToStyle(state.penColor);
  offCtx.fillText(text, Number(x), Number(y));
  offCtx.restore();
}

export function painter_fill(x, y) {
  if (!offCtx) return;
  const px = Number(x), py = Number(y);
  if (px < 0 || py < 0 || px >= state.sheetW || py >= state.sheetH) return;

  const c = BigInt.asUintN(32, state.hasBrush ? state.brushColor : 0xFF000000n);
  const fr = Number((c >> 16n) & 0xFFn);
  const fg = Number((c >>  8n) & 0xFFn);
  const fb = Number( c         & 0xFFn);
  const fa = Number((c >> 24n) & 0xFFn);

  const imageData = offCtx.getImageData(0, 0, state.sheetW, state.sheetH);
  floodFill(imageData, px, py, fr, fg, fb, fa);
  offCtx.putImageData(imageData, 0, 0);
}

// ── Sheet management ──────────────────────────────────────────────────────────

export function painter_new_sheet(w, h, color) {
  const sw = Number(w), sh = Number(h);
  if (sw <= 0 || sh <= 0 || sw > 32767 || sh > 32767) return;
  state.sheetW = sw; state.sheetH = sh;
  offscreen = makeOffscreen(sw, sh);
  offCtx = offscreen ? offscreen.getContext('2d') : null;
  if (offCtx) {
    offCtx.fillStyle = argbToStyle(color);
    offCtx.fillRect(0, 0, sw, sh);
  }
}

export function __flushPainter() {
  scheduleDraw();
}

export function painter_load_sheet(charPtr) {
  const filename = readCString(charPtr);
  console.warn(`painter_load_sheet("${filename}"): not supported in browser runtime`);
}

export function painter_save_sheet(charPtr) {
  if (!offscreen) return;
  const filename = readCString(charPtr) || 'sheet.png';
  offscreen.convertToBlob({ type: 'image/png' }).then(blob => {
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url; a.download = filename;
    a.click();
    URL.revokeObjectURL(url);
  });
}
