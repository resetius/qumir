(block
  (pragma language overloads)

  (fun <main> ()
    (block
      (output (call wrap (: 5 i64)) "\n")
      (output (call wrap "hi") "\n")))

  (fun identity ((var x <named K (template)>)) -> <named K (template)>
    (block
      (return x)))

  (fun wrap ((var y <named T (template)>)) -> <named T (template)>
    (block
      (return (call identity y)))))
