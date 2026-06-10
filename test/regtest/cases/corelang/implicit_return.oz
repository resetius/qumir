(block
  (fun <main> ()
    (block
      (output (call square (: 5 i64)) "\n")
      (output (call cube (: 3 i64)) "\n")))

  (fun square ((var x i64)) -> i64
    (block
      (* x x)))

  (fun cube ((var x i64)) -> i64
    (block
      (var sq i64)
      (= sq (* x x))
      (* sq x))))
