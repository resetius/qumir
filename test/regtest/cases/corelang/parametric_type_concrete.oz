(block
  (type Box [T] <struct (x T)>)

  (fun <main> ()
    (block
      (var boxed <named Box [i64]>)
      (= boxed (cast (struct ((x (: 10 i64)))) <named Box [i64]>))
      (output (field boxed x) "\n"))))
