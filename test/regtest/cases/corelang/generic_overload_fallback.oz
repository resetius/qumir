(block
  (pragma language overloads)

  (fun <main> ()
    (block
      (output (call f (: 5 i64)) "\n")
      (output (call f "hi") "\n")))

  (fun f ((var x i64)) -> i64
    (block
      (var знач i64)
      (= знач 100)))

  (fun f ((var x <named K (template)>)) -> i64
    (block
      (var знач i64)
      (= знач 200))))
