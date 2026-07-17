(block
  (pragma language overloads)

  (fun <main> ()
    (block
      (output (call f (: 5 i64)) "\n")
      (output (call f "hi") "\n")))

  (fun f ((var x i64)) -> i64
    (block
      (return 100)))

  (fun f [K] ((var x K)) -> i64
    (block
      (return 200))))
