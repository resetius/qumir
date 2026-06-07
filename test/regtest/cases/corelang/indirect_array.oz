(block
  (fun <main> void () ()
    (block
      (var ops <array <fun i64 (i64 i64)> 1> [0 1])
      (= ops [0] add)
      (= ops [1] mul)
      (output (call (index 0 ops) (: 5 i64) (: 6 i64)) "\n")
      (output (call (index 1 ops) (: 5 i64) (: 6 i64)) "\n")))

  (fun add i64 ((var a i64) (var b i64)) ()
    (block
      (var $$return i64)
      (= $$return (+ a b))))

  (fun mul i64 ((var a i64) (var b i64)) ()
    (block
      (var $$return i64)
      (= $$return (* a b)))))
