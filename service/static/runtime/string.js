export function str_from_lit(ptr) { return String(ptr); }

export function str_retain(_ptr) {}
export function str_release(_ptr) {}

export function str_concat(a, b) {
    return String(a) + String(b);
}

export function str_compare(a, b) {
    const sa = String(a);
    const sb = String(b);
    if (sa === sb) return 0;
    return sa < sb ? -1 : 1;
}
