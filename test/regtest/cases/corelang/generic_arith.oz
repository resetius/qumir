(block
  (pragma language overloads)

  (fun <main> ()
    (block
      (output (call compute (: 3 i64)) "\n")
      (output (call compute (: 2.5 f64)) "\n")))

  (fun compute ((var x <named T (template readable mutable)>)) -> <named T (template readable mutable)>
    (block
      (var $$return <named T (template readable mutable)>)
      (= $$return (+ (* x x) (- (* x (: 2 i64)) (: 1 i64)))))))
