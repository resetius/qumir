(block
  (use vector)
  (fun <main> ()
    (block
      (output (vec 1 2) "\n")
      (output (vec 1 2 3 4) "\n")
      (output (vec 1 2 3 4 5 6 7 8) "\n")
      (output (vec 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16) "\n")
      (var v = (vec 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16
        17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32))
      (output (+ v 1) "\n")
      (output (index v 31) "\n"))))
