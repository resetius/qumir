(block
  (pragma language overloads)
  (fun pick i64 ((var x i64)) ()
    (block
      (var $$return i64)
      (= $$return (+ x (: 10 i64)))))

  (fun pick f64 ((var x f64)) ()
    (block
      (var $$return f64)
      (= $$return (+ x (: 0.5 f64)))))

  (fun pick string ((var x string)) ()
    (block
      (var $$return string)
      (= $$return x)))

  (fun <main> void () ()
    (block
      (output (call pick (: 7 i32)) "\n")
      (output (call pick (: 2.5 f64)) "\n")
      (output (call pick "text") "\n"))))
