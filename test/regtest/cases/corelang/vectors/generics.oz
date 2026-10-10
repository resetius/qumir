; disable_llvm
(block
  (pragma language overloads)
  (use vector)
  (type V [T (const N i64)] <vec T N>)
  (fun last [T (const N i64)] ((var v <ref <vec T N>>)) -> T
    (block (return (index v (- N 1)))))
  (fun <main> ()
    (block
      (var alias = (: (vec -1 127) <named V [i8 2]>))
      (var a = (: (vec -1 127) <vec i8 2>))
      (var b = (vec 1 2 3 4))
      (output a " " (call last a) "\n")
      (output (index alias 0) " " (index alias 1) "\n")
      (output b " " (call last b) "\n")
      (output (vec #t #f) "\n")
      (output (vec 1.5 -2.5) "\n")
      (output a " " a "\n"))))
