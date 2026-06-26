(block
  ; extern taking a by-value struct: C `double cabs(double complex)`, where
  ; `double complex` is laid out as {re; im} (two SSE eightbytes).
  (type комплекс <struct (re f64) (im f64)>)
  (fun cabs ((var z комплекс)) -> f64 (attrs extern)
    (block))
  (fun <main> ()
    (block
      (var z комплекс)
      (= z (: (struct ((re (: 3.0 f64)) (im (: 4.0 f64)))) комплекс))
      (output (call cabs z) "\n"))))
