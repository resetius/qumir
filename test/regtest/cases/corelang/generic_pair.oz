(block
  (pragma language overloads)

  (fun <main> void () ()
    (block
      (output (call pairFirst (: 3 i64) "x") "\n")
      (output (call pairFirst "ab" (: 10 i64)) "\n")
      (output (call pairSecond (: 3 i64) "x") "\n")
      (output (call pairSecond "ab" (: 10 i64)) "\n")))

  (fun plus i64 ((var a i64) (var b i64)) ()
    (block
      (var $$return i64)
      (= $$return (+ a b))))

  (fun plus string ((var a string) (var b string)) ()
    (block
      (var $$return string)
      (= $$return (+ a b))))

  (fun pairFirst <named K (template readable mutable)>
       ((var a <named K (template readable mutable)>)
        (var b <named V (template readable mutable)>)) ()
    (block
      (var $$return <named K (template readable mutable)>)
      (= $$return (call plus a a))))

  (fun pairSecond <named V (template readable mutable)>
       ((var a <named K (template readable mutable)>)
        (var b <named V (template readable mutable)>)) ()
    (block
      (var $$return <named V (template readable mutable)>)
      (= $$return (call plus b b)))))
