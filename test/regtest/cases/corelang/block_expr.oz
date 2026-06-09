(block
  (fun <main> ()
    (block
      (var x i64)
      (= x (block
              (var a i64)
              (= a 10)
              a))
      (output x "\n"))))
