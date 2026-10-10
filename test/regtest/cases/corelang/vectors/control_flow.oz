(block
  (use vector)
  (fun choose ((var v <ref <vec i64 2>>) (var left bool)) -> i64
    (block (return (if left (index v 0) (index v 1)))))
  (fun <main> ()
    (block
      (var a = (vec 10 20))
      (var b = (vec 1 2))
      (output (call choose a #t) " " (call choose a #f) "\n")
      (var v = (if (== (call choose a #t) 10) (+ a b) (- a b)))
      (output v "\n")
      (var w = (if (== (call choose a #f) 10) (+ a b) (- a b)))
      (output w "\n"))))
