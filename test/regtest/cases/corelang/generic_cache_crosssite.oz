(block
  (pragma language overloads)

  (fun <main> ()
    (block
      (output (call useA (: 1 i64)) "\n")
      (output (call useB (: 2 i64)) "\n")
      (output (call identity (: 3 i64)) "\n")
      (output (call identity "x") "\n")))

  (fun useA ((var x i64)) -> i64
    (block
      (return (call identity x))))

  (fun useB ((var x i64)) -> i64
    (block
      (return (call identity x))))

  (fun identity [K] ((var x K)) -> K
    (block
      (return x))))
