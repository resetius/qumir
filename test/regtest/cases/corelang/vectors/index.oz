; disable_llvm
(block
  (fun <main> ()
    (block
      (var v = (: (vec -128 127) <vec i8 2>))
      (var i = 0)
      (output (index v i) " " (index v (+ i 1)) "\n")
      (output (index (vec -7 9) 0) "\n")
      (output (index (+ (vec 1 2) (vec 3 4)) 1) "\n")
      (output (index (if #t (vec 10 20) (vec 30 40)) 1) "\n")
      (var u = (: (vec 0 255) <vec u8 2>))
      (output (index u 0) " " (index u 1) "\n")
      (var f = (vec -1.25 2.5))
      (output (index f 0) " " (index f 1) "\n")
      (var b = (vec #t #f))
      (output (index b 0) " " (index b 1) "\n"))))
