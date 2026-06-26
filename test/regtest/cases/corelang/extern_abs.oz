(block
  ; extern with default symbol: oz name == native symbol (llabs)
  (fun llabs ((var x i64)) -> i64 (attrs extern)
    (block))
  ; extern with explicit symbol: oz name my_abs bound to native llabs
  (fun my_abs ((var x i64)) -> i64 (attrs (extern llabs))
    (block))
  (fun <main> ()
    (block
      (output (call llabs (cast (: -5 i64) i64)) " ")
      (output (call my_abs (cast (: -7 i64) i64)) "\n"))))
