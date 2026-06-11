(block
  (fun <main> ()
    (block
      (var all_bits = (cast (: -1 i64) u64))
      (output (cast (>> all_bits (: 1 u64)) i64) "\n")
      (output (cast (xor all_bits (: 1 u64)) i64) "\n")
      (output (cast (<< (: 1 u64) (: 63 u64)) i64) "\n"))))
