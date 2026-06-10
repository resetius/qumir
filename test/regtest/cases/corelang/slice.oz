(block
  (fun <main> ()
    (block
      (var s string)
      (= s "hello world")
      (var a string)
      (= a (slice s [1 5]))
      (var b string)
      (= b (slice s [7]))
      (output a " " b "\n"))))
