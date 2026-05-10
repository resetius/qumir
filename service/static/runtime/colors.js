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

// Exported utility for painter.js and drawer.js
export function argbToStyle(color) {
  const c = BigInt.asUintN(32, BigInt(color));
  const a = Number((c >> 24n) & 0xFFn);
  const r = Number((c >> 16n) & 0xFFn);
  const g = Number((c >>  8n) & 0xFFn);
  const b = Number( c         & 0xFFn);
  return `rgba(${r},${g},${b},${(a / 255).toFixed(6)})`;
}

// ── Color decomposition ───────────────────────────────────────────────────────

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
