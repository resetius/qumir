(block
  (pragma language overloads)
  (fun pick ((var x i64)) -> i64
    (block
      (var знач i64)
      (= знач (+ x (: 10 i64)))))

  (fun pick ((var x f64)) -> f64
    (block
      (var знач f64)
      (= знач (+ x (: 0.5 f64)))))

  (fun pick ((var x string)) -> string
    (block
      (var знач string)
      (= знач x)))

  (fun <main> ()
    (block
      (output (call pick (: 7 i32)) "\n")
      (output (call pick (: 2.5 f64)) "\n")
      (output (call pick "text") "\n"))))
