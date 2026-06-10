(block
  (pragma language overloads)
  (fun pick ((var x i64)) -> i64
    (block
      (return (+ x (: 10 i64)))))

  (fun pick ((var x f64)) -> f64
    (block
      (return (+ x (: 0.5 f64)))))

  (fun pick ((var x string)) -> string
    (block
      (return x)))

  (fun <main> ()
    (block
      (output (call pick (: 7 i32)) "\n")
      (output (call pick (: 2.5 f64)) "\n")
      (output (call pick "text") "\n"))))
