"use strict";

let MEMORY = null;

export function __bindMemory(mem) {
  MEMORY = mem;
}

function view() {
  if (!MEMORY) {
    throw new Error("complex runtime called before __bindMemory");
  }
  return new DataView(MEMORY.buffer);
}

function ptr(v) {
  return Number(v) >>> 0;
}

function writeComplex(p, re, im) {
  const base = ptr(p);
  const dv = view();
  dv.setFloat64(base, Number(re), true);
  dv.setFloat64(base + 8, Number(im), true);
}

export function complex_i(r) {
  writeComplex(r, 0.0, 1.0);
}

export function complex_re(re, im) {
  return Number(re);
}

export function complex_im(re, im) {
  return Number(im);
}

export function complex_abs(re, im) {
  return Math.hypot(Number(re), Number(im));
}

export function complex_arg(re, im) {
  return Math.atan2(Number(im), Number(re));
}

export function complex_conj(r, re, im) {
  writeComplex(r, re, -Number(im));
}

export function complex_add(r, ar, ai, br, bi) {
  writeComplex(r, Number(ar) + Number(br), Number(ai) + Number(bi));
}

export function complex_sub(r, ar, ai, br, bi) {
  writeComplex(r, Number(ar) - Number(br), Number(ai) - Number(bi));
}

export function complex_mul(r, ar, ai, br, bi) {
  ar = Number(ar);
  ai = Number(ai);
  br = Number(br);
  bi = Number(bi);
  writeComplex(r, ar * br - ai * bi, ar * bi + ai * br);
}

export function complex_div(r, ar, ai, br, bi) {
  ar = Number(ar);
  ai = Number(ai);
  br = Number(br);
  bi = Number(bi);
  const denom = br * br + bi * bi;
  writeComplex(r, (ar * br + ai * bi) / denom, (ai * br - ar * bi) / denom);
}

export function complex_neg(r, re, im) {
  writeComplex(r, -Number(re), -Number(im));
}

export function complex_eq(ar, ai, br, bi) {
  return (Number(ar) === Number(br) && Number(ai) === Number(bi)) ? 1 : 0;
}

export function complex_ne(ar, ai, br, bi) {
  return (Number(ar) !== Number(br) || Number(ai) !== Number(bi)) ? 1 : 0;
}

export function complex_from_float(r, x) {
  writeComplex(r, Number(x), 0.0);
}

export function complex_from_int(r, n) {
  writeComplex(r, Number(n), 0.0);
}

export function complex_to_float(re, im) {
  return Number(re);
}

export function complex_to_int(re, im) {
  return BigInt(Math.trunc(Number(re)));
}
