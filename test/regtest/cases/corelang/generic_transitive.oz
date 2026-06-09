(block
  (pragma language overloads)

  (fun <main> ()
    (block
      (output (call wrap (: 5 i64)) "\n")
      (output (call wrap "hi") "\n")))

  (fun identity ((var x <named K (template readable mutable)>)) -> <named K (template readable mutable)>
    (block
      (var знач <named K (template readable mutable)>)
      (= знач x)))

  (fun wrap ((var y <named T (template readable mutable)>)) -> <named T (template readable mutable)>
    (block
      (var знач <named T (template readable mutable)>)
      (= знач (call identity y)))))
