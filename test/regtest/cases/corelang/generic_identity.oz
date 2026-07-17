(block
  (pragma language overloads)

  (fun <main> ()
    (block
      (output (call identity (: 5 i64)) "\n")
      (output (call identity "hello") "\n")
      (output (call identity (: 3.5 f64)) "\n")
      (output (call identity (: 7 i64)) "\n")))

  (fun identity [K] ((var x K)) -> K
    (block
      (return x))))
