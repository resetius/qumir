(block
  (pragma language overloads)

  (fun <main> ()
    (block
      (output (call pairFirst (: 3 i64) "x") "\n")
      (output (call pairFirst "ab" (: 10 i64)) "\n")
      (output (call pairSecond (: 3 i64) "x") "\n")
      (output (call pairSecond "ab" (: 10 i64)) "\n")))

  (fun plus ((var a i64) (var b i64)) -> i64
    (block
      (return (+ a b))))

  (fun plus ((var a string) (var b string)) -> string
    (block
      (return (+ a b))))

  (fun pairFirst [K V] ((var a K)
        (var b V)) -> K
    (block
      (return (call plus a a))))

  (fun pairSecond [K V] ((var a K)
        (var b V)) -> V
    (block
      (return (call plus b b)))))
