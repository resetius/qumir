(block
  (pragma language overloads)
  (fun double ((var x i32)) -> i32
    (block
      (return (* x 2))))
  (fun double ((var x f64)) -> f64
    (block
      (return (* x (: 2.0 f64)))))
  (fun <main> ()
    (block
      (output (call double (: 3 i32)) "\n")
      (output (call double (: 2.5 f64)) "\n"))))
