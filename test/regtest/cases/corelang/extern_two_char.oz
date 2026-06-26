(block
  ; symbols defined in test_reg.cpp
  (type two_char <struct (a i8) (b i8)>)
  (fun qumir_two_char_sum ((var p two_char)) -> i64 (attrs extern)
    (block))
  (fun qumir_two_char_swap ((var p two_char)) -> two_char (attrs extern)
    (block))
  (fun <main> ()
    (block
      (var p two_char)
      (= p (: (struct ((a (: 3 i8)) (b (: 4 i8)))) two_char))
      (output (call qumir_two_char_sum p) " ")
      (var q two_char)
      (= q (call qumir_two_char_swap p))
      (output (field q a) " " (field q b) "\n"))))
