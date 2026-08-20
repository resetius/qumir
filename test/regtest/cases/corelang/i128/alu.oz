(block
  (fun <main> ()
    (block
      (var a i128)
      (= a (: 1000000000000 i128))
      (var b i128)
      (= b (: 1000000000 i128))
      ; 10^21 does not fit into 64 bits, so every half below is 128-bit specific
      (var wide i128)
      (= wide (* a b))

      (output "mul:  " (cast (>> wide (: 64 i128)) i64) " " (cast wide i64) "\n")
      (output "add:  " (cast (>> (+ wide a) (: 64 i128)) i64) " " (cast (+ wide a) i64) "\n")
      (output "sub:  " (cast (>> (- wide a) (: 64 i128)) i64) " " (cast (- wide a) i64) "\n")
      (output "neg:  " (cast (>> (- wide) (: 64 i128)) i64) " " (cast (- wide) i64) "\n")

      (output "shl:  " (cast (>> (<< a (: 64 i128)) (: 64 i128)) i64) "\n")
      (output "shr:  " (cast (>> wide (: 40 i128)) i64) "\n")
      (output "sar:  " (cast (>> (- wide) (: 40 i128)) i64) "\n")

      (output "and:  " (cast (& wide a) i64) "\n")
      (output "or:   " (cast (>> (| wide a) (: 64 i128)) i64) " " (cast (| wide a) i64) "\n")
      (output "xor:  " (cast (>> (^ wide a) (: 64 i128)) i64) " " (cast (^ wide a) i64) "\n")
      (output "not:  " (cast (>> (~ wide) (: 64 i128)) i64) " " (cast (~ wide) i64) "\n")

      (output "lt:   " (< a wide) " " (< wide a) "\n")
      (output "gt:   " (> wide a) " " (> a wide) "\n")
      (output "le:   " (<= wide wide) " " (<= wide a) "\n")
      (output "ge:   " (>= wide wide) " " (>= a wide) "\n")
      (output "eq:   " (== wide wide) " " (== wide a) "\n")
      (output "ne:   " (!= wide a) " " (!= wide wide) "\n")

      (var u u128)
      (= u (: 1000000000000 u128))
      (var uwide u128)
      (= uwide (* u (: 1000000000 u128)))
      (output "umul: " (cast (>> uwide (: 64 u128)) i64) " " (cast uwide i64) "\n")
      ; unsigned shift keeps the sign bit out, unlike the signed one above
      (output "ushr: " (cast (>> (~ uwide) (: 120 u128)) i64) "\n")
      (output "sshr: " (cast (>> (~ wide) (: 120 i128)) i64) "\n")
      (output "ult:  " (< u uwide) " " (< uwide u) "\n")

      (var narrow i64)
      (= narrow (: -5 i64))
      (output "sext: " (cast (>> (cast narrow i128) (: 64 i128)) i64) " " (cast (cast narrow i128) i64) "\n")
      (var unarrow u64)
      (= unarrow (: 5 u64))
      (output "zext: " (cast (>> (cast unarrow u128) (: 64 u128)) i64) " " (cast (cast unarrow u128) i64) "\n")
      (output "trunc:" (cast wide i64) "\n")
      (output "toflt:" (/ (cast wide f64) 1e21) "\n")
      (output "toint:" (cast (>> (cast 1e21 i128) (: 64 i128)) i64) "\n"))))
