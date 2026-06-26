(block
  ; extern with a floating-point signature, resolved against libm
  (fun fabs ((var x f64)) -> f64 (attrs extern)
    (block))
  (fun <main> ()
    (block
      (output (call fabs (: -3.5 f64)) "\n"))))
