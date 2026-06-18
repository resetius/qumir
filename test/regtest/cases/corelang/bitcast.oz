(block
  (fun <main> ()
    (block
      (var value = -1.5)
      (var bits = (bitcast value u64))
      (var roundtrip = (bitcast bits f64))
      (var literal = (bitcast 4609434218613702656 f64))
      (output (cast bits i64) " " roundtrip " " literal "\n"))))
