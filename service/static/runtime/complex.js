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

function readComplex(p) {
  const base = ptr(p);
  const dv = view();
  return {
    re: dv.getFloat64(base, true),
    im: dv.getFloat64(base + 8, true),
  };
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

export function complex_re(a) {
  return readComplex(a).re;
}

export function complex_im(a) {
  return readComplex(a).im;
}

export function complex_abs(a) {
  const z = readComplex(a);
  return Math.hypot(z.re, z.im);
}

export function complex_arg(a) {
  const z = readComplex(a);
  return Math.atan2(z.im, z.re);
}

export function complex_conj(r, a) {
  const z = readComplex(a);
  writeComplex(r, z.re, -z.im);
}

export function complex_add(r, a, b) {
  const x = readComplex(a);
  const y = readComplex(b);
  writeComplex(r, x.re + y.re, x.im + y.im);
}

export function complex_sub(r, a, b) {
  const x = readComplex(a);
  const y = readComplex(b);
  writeComplex(r, x.re - y.re, x.im - y.im);
}

export function complex_mul(r, a, b) {
  const x = readComplex(a);
  const y = readComplex(b);
  writeComplex(r, x.re * y.re - x.im * y.im, x.re * y.im + x.im * y.re);
}

export function complex_div(r, a, b) {
  const x = readComplex(a);
  const y = readComplex(b);
  const denom = y.re * y.re + y.im * y.im;
  writeComplex(r, (x.re * y.re + x.im * y.im) / denom, (x.im * y.re - x.re * y.im) / denom);
}

export function complex_neg(r, a) {
  const z = readComplex(a);
  writeComplex(r, -z.re, -z.im);
}

export function complex_eq(a, b) {
  const x = readComplex(a);
  const y = readComplex(b);
  return (x.re === y.re && x.im === y.im) ? 1 : 0;
}

export function complex_ne(a, b) {
  const x = readComplex(a);
  const y = readComplex(b);
  return (x.re !== y.re || x.im !== y.im) ? 1 : 0;
}

export function complex_from_float(r, x) {
  writeComplex(r, Number(x), 0.0);
}

export function complex_from_int(r, n) {
  writeComplex(r, Number(n), 0.0);
}

export function complex_to_float(a) {
  return readComplex(a).re;
}

export function complex_to_int(a) {
  return BigInt(Math.trunc(readComplex(a).re));
}
