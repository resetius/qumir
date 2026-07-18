; disable_exec
(block
  (type Nullable [T] <struct (Value T) (Valid bool)>)

  (fun <main> ()
    (block
      (var wrong <struct (Payload i64) (Ready bool)>)
      (var value <named Nullable [i64]>)
      (= wrong (struct ((Payload (: 10 i64)) (Ready #t))))
      (= value (cast wrong <named Nullable [i64]>)))))
