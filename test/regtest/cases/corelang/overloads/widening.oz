(block
  (pragma language overloads)
  (fun square i64 ((var x i64)) ()
    (block
      (var $$return i64)
      (= $$return (* x x))))

  (fun square f64 ((var x f64)) ()
    (block
      (var $$return f64)
      (= $$return (* x x))))

  (fun <main> void () ()
    (block
      (output (call square (: 5 i32)) "\n")
      (output (call square (: 3.0 f64)) "\n"))))
