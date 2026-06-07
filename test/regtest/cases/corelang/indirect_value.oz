(block
  (fun <main> void () ()
    (block
      (var f <fun i64 (i64 i64)>)
      (= f add)
      (output (call apply add (: 3 i64) (: 4 i64)) "\n")
      (output (call apply mul (: 3 i64) (: 4 i64)) "\n")
      (output (call f (: 10 i64) (: 20 i64)) "\n")))

  (fun add i64 ((var a i64) (var b i64)) ()
    (block
      (var $$return i64)
      (= $$return (+ a b))))

  (fun mul i64 ((var a i64) (var b i64)) ()
    (block
      (var $$return i64)
      (= $$return (* a b))))

  (fun apply i64 ((var f <fun i64 (i64 i64)>) (var x i64) (var y i64)) ()
    (block
      (var $$return i64)
      (= $$return (call f x y)))))
