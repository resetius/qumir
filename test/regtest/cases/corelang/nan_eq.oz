(block
  (fun <main> ()
    (block
      (var nan = (bitcast -1 f64))
      (var x = 1.0)

      (output
        (< nan x) " "
        (<= nan x) " "
        (> nan x) " "
        (>= nan x) " "
        (== nan x) " "
        (!= nan x) "\n"

        (== nan nan) " "
        (!= nan nan) "\n"))))