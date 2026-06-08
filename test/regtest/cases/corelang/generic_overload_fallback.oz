(block
  (pragma language overloads)

  (fun <main> void () ()
    (block
      (output (call f (: 5 i64)) "\n")
      (output (call f "hi") "\n")))

  (fun f i64 ((var x i64)) ()
    (block
      (var $$return i64)
      (= $$return 100)))

  (fun f i64 ((var x <named K (template)>)) ()
    (block
      (var $$return i64)
      (= $$return 200))))
