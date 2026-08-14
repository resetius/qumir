(block
  (fun <main> ()
    (block
      (var a = (: 1 i64))
      (var b = (: 2 i64))
      (var c = (: 3 i64))

      (output (+ a b c 4) "\n")
      (output (- 10 a b c) "\n")
      (output (* a b c 2) "\n")
      (output (/ 120 a b c) "\n")

      (output (& 7 6 5) "\n")
      (output (| 1 2 4) "\n")
      (output (xor 1 2 4) "\n")

      (output (&& (> a 0) (> b 0) (> c 0)) "\n")
      (output (|| (< a 0) (< b 0) (== c 3)) "\n"))))
