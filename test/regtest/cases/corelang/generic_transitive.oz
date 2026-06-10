(block
  (pragma language overloads)

  (fun <main> ()
    (block
      (output (call wrap (: 5 i64)) "\n")
      (output (call wrap "hi") "\n")))

  (fun identity ((var x <named K (template readable mutable)>)) -> <named K (template readable mutable)>
    (block
      (return x)))

  (fun wrap ((var y <named T (template readable mutable)>)) -> <named T (template readable mutable)>
    (block
      (return (call identity y)))))
