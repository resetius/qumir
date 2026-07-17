(block
  (pragma language overloads)

  (fun <main> ()
    (block
      (output (call wrap (: 5 i64)) "\n")
      (output (call wrap "hi") "\n")))

  (fun identity [K] ((var x K)) -> K
    (block
      (return x)))

  (fun wrap [T] ((var y T)) -> T
    (block
      (return (call identity y)))))
