(block
  (pragma language overloads)
  (fun double i32 ((var x i32)) ()
    (block
      (var $$return i32)
      (= $$return (* x 2))))
  (fun double f64 ((var x f64)) ()
    (block
      (var $$return f64)
      (= $$return (* x (: 2.0 f64)))))
  (fun <main> void () ()
    (block
      (output (call double (: 3 i32)) "\n")
      (output (call double (: 2.5 f64)) "\n"))))
