(block
  (pragma language overloads)

  (fun <main> void () ()
    (block
      (output (call useA (: 1 i64)) "\n")
      (output (call useB (: 2 i64)) "\n")
      (output (call identity (: 3 i64)) "\n")
      (output (call identity "x") "\n")))

  (fun useA i64 ((var x i64)) ()
    (block
      (var $$return i64)
      (= $$return (call identity x))))

  (fun useB i64 ((var x i64)) ()
    (block
      (var $$return i64)
      (= $$return (call identity x))))

  (fun identity <named K (template readable mutable)> ((var x <named K (template readable mutable)>)) ()
    (block
      (var $$return <named K (template readable mutable)>)
      (= $$return x))))
