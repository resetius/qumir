(block
  (fun <main> ()
    (block
      ; '/' converts to float, '//' stays in the integers
      (output "div:   " (/ (: 7 i64) (: 2 i64)) "\n")
      (output "idiv:  " (// (: 7 i64) (: 2 i64)) "\n")
      (output "rem:   " (% (: 7 i64) (: 2 i64)) "\n")

      ; truncation goes toward zero, so the remainder keeps the left sign
      (output "neg:   " (// (: -7 i64) (: 2 i64)) " " (% (: -7 i64) (: 2 i64)) "\n")
      (output "negr:  " (// (: 7 i64) (: -2 i64)) " " (% (: 7 i64) (: -2 i64)) "\n")
      (output "both:  " (// (: -7 i64) (: -2 i64)) " " (% (: -7 i64) (: -2 i64)) "\n")

      ; '//' folds left, like the other associative operators
      (output "fold:  " (// (: 100 i64) (: 5 i64) (: 2 i64)) "\n")

      ; unsigned division must not treat the top bit as a sign
      (var u u64)
      (= u (~ (: 0 u64)))
      (output "udiv:  " (cast (// u (: 3 u64)) i64) " " (cast (% u (: 3 u64)) i64) "\n")

      ; narrow widths keep their own type
      (var b u8)
      (= b (: 200 u8))
      (output "u8:    " (cast (// b (: 3 u8)) i64) " " (cast (% b (: 3 u8)) i64) "\n")
      (var s i16)
      (= s (: -300 i16))
      (output "i16:   " (cast (// s (: 7 i16)) i64) " " (cast (% s (: 7 i16)) i64) "\n")

      ; 128-bit operands exceed what a 64-bit divide could answer
      (var w i128)
      (= w (* (: 1000000000000 i128) (: 1000000000 i128)))
      (output "i128:  " (cast (// w (: 1000000000000 i128)) i64) " " (cast (% w (: 7 i128)) i64) "\n")
      (var uw u128)
      (= uw (~ (: 0 u128)))
      (output "u128:  " (cast (// uw (: 3 u128)) i64) " " (cast (% uw (: 3 u128)) i64) "\n")

      ; a mixed-width pair widens to the common type before the divide
      (var wide i64)
      (= wide (: 1000000 i64))
      (output "mixed: " (// wide (cast (: 7 i16) i64)) " " (% wide (cast (: 7 i16) i64)) "\n"))))
