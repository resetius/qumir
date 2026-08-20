(block
  (fun <main> ()
    (block
      (var arr <array i128 1> [0 3])
      (var i i64)
      (var base i128)
      (= base (* (: 1000000000000 i128) (: 1000000000 i128)))

      (= i (: 0 i64))
      (while (<= i (: 3 i64))
        (block
          (= arr [i] (+ base (cast i i128)))
          (= i (+ i (: 1 i64)))))

      (= i (: 0 i64))
      (while (<= i (: 3 i64))
        (block
          (output
            (cast (>> (index arr i) (: 64 i128)) i64)
            " "
            (cast (index arr i) i64)
            "\n")
          (= i (+ i (: 1 i64)))))

      ; every element must keep its own 128 bits, not alias a neighbour
      (var sum i128)
      (= sum (- (index arr (: 3 i64)) (index arr (: 0 i64))))
      (output "spread: " (cast sum i64) "\n")

      (var uarr <array u128 1> [0 1])
      (= uarr [0] (~ (: 0 u128)))
      (= uarr [1] (: 1 u128))
      (output "umax: " (cast (>> (index uarr (: 0 i64)) (: 120 u128)) i64) "\n")
      (output "uone: " (cast (index uarr (: 1 i64)) i64) "\n"))))
