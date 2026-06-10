(block
  (pragma language overloads)
  (fun square ((var x i64)) -> i64
    (block
      (return (* x x))))

  (fun square ((var x f64)) -> f64
    (block
      (return (* x x))))

  (fun <main> ()
    (block
      (output (call square (: 5 i32)) "\n")
      (output (call square (: 3.0 f64)) "\n"))))
