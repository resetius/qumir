(block
  ; Regression for wasm32 struct layout: a Ptr field must sit at its real
  ; 4-byte-aligned offset under wasm32-unknown-unknown, not at the 8-byte
  ; offset used on 64-bit targets, otherwise the following field is read
  ; out of bounds (UB that -O3 can miscompile away).
  (type S <struct (a i8) (s <ptr i8>) (c i64)>)

  (fun <main> ()
    (block
      (var v S)
      (= v (: (struct ((a (: 7 i8)) (s (cast (: 1234 i64) <ptr i8>)) (c (: 987654321 i64)))) S))
      (output (cast (field v a) i64) " "
              (cast (field v s) i64) " "
              (field v c) "\n"))))
