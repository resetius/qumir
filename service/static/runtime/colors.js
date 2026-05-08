"use strict";

// Colors (Цвета) runtime — ARGB color values as BigInt (0xAARRGGBB)

import { __appendStdout } from './io.js';

let MEMORY = null;

export function __bindMemory(mem) { MEMORY = mem; }

// ── Internal helpers ──────────────────────────────────────────────────────────

function wasmAddr(ptr) { return Number(BigInt.asUintN(32, BigInt(ptr))); }

function writeI64(ptr, value) {
  if (!MEMORY) return;
  new DataView(MEMORY.buffer).setBigInt64(wasmAddr(ptr), BigInt(value), true);
}

function packARGB(a, r, g, b) {
  return (BigInt(a & 0xFF) << 24n) | (BigInt(r & 0xFF) << 16n) |
         (BigInt(g & 0xFF) <<  8n) |  BigInt(b & 0xFF);
}

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

// Exported utility for painter.js and drawer.js
export function argbToStyle(color) {
  const c = BigInt.asUintN(32, BigInt(color));
  const a = Number((c >> 24n) & 0xFFn);
  const r = Number((c >> 16n) & 0xFFn);
  const g = Number((c >>  8n) & 0xFFn);
  const b = Number( c         & 0xFFn);
  return `rgba(${r},${g},${b},${(a / 255).toFixed(6)})`;
}

// ── Color constants ───────────────────────────────────────────────────────────

export function color_transparent() { return 0x00000000n; }
export function color_white()       { return 0xFFFFFFFFn; }
export function color_black()       { return 0xFF000000n; }
export function color_gray()        { return 0xFF808080n; }
export function color_purple()      { return 0xFF800080n; }
export function color_blue()        { return 0xFF0000FFn; }
export function color_cyan()        { return 0xFF00FFFFn; }
export function color_green()       { return 0xFF008000n; }
export function color_yellow()      { return 0xFFFFFF00n; }
export function color_orange()      { return 0xFFFFA500n; }
export function color_red()         { return 0xFFFF0000n; }

// ── Color constructors ────────────────────────────────────────────────────────

export function color_rgb(r, g, b)       { return packARGB(255, Number(r), Number(g), Number(b)); }
export function color_rgba(r, g, b, a)   { return packARGB(Number(a), Number(r), Number(g), Number(b)); }

export function color_hsl(h, s, l)       { const [r,g,b] = hslToRGB(Number(h),Number(s),Number(l)); return packARGB(255,r,g,b); }
export function color_hsla(h, s, l, a)   { const [r,g,b] = hslToRGB(Number(h),Number(s),Number(l)); return packARGB(Number(a),r,g,b); }
export function color_hsv(h, s, v)       { const [r,g,b] = hsvToRGB(Number(h),Number(s),Number(v)); return packARGB(255,r,g,b); }
export function color_hsva(h, s, v, a)   { const [r,g,b] = hsvToRGB(Number(h),Number(s),Number(v)); return packARGB(Number(a),r,g,b); }
export function color_cmyk(c, m, y, k)   { const [r,g,b] = cmykToRGB(Number(c),Number(m),Number(y),Number(k)); return packARGB(255,r,g,b); }
export function color_cmyka(c,m,y,k,a)   { const [r,g,b] = cmykToRGB(Number(c),Number(m),Number(y),Number(k)); return packARGB(Number(a),r,g,b); }

// ── Color decomposition ───────────────────────────────────────────────────────

export function color_decompose_rgb(color, rPtr, gPtr, bPtr) {
  const c = BigInt.asUintN(32, BigInt(color));
  writeI64(rPtr, (c >> 16n) & 0xFFn);
  writeI64(gPtr, (c >>  8n) & 0xFFn);
  writeI64(bPtr,  c         & 0xFFn);
}

export function color_decompose_cmyk(color, cPtr, mPtr, yPtr, kPtr) {
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

export function color_decompose_hsl(color, hPtr, sPtr, lPtr) {
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

export function color_decompose_hsv(color, hPtr, sPtr, vPtr) {
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

export function color_print(color) {
  const c = BigInt.asUintN(32, BigInt(color));
  const r = Number((c >> 16n) & 0xFFn);
  const g = Number((c >>  8n) & 0xFFn);
  const b = Number( c         & 0xFFn);
  const hex = '#' + [r, g, b].map(v => v.toString(16).padStart(2, '0').toUpperCase()).join('');
  __appendStdout(hex);
}
