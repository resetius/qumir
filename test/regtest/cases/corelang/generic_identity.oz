(block
  (pragma language overloads)

  (fun <main> void () ()
    (block
      (output (call identity (: 5 i64)) "\n")
      (output (call identity "hello") "\n")
      (output (call identity (: 3.5 f64)) "\n")
      (output (call identity (: 7 i64)) "\n")))

  (fun identity <named K (template readable mutable)> ((var x <named K (template readable mutable)>)) ()
    (block
      (var $$return <named K (template readable mutable)>)
      (= $$return x))))
