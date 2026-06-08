(block
  (pragma language overloads)

  (fun <main> void () ()
    (block
      (output (call wrap (: 5 i64)) "\n")
      (output (call wrap "hi") "\n")))

  (fun identity <named K (template readable mutable)> ((var x <named K (template readable mutable)>)) ()
    (block
      (var $$return <named K (template readable mutable)>)
      (= $$return x)))

  (fun wrap <named T (template readable mutable)> ((var y <named T (template readable mutable)>)) ()
    (block
      (var $$return <named T (template readable mutable)>)
      (= $$return (call identity y)))))
