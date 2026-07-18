(block
  (pragma language overloads)
  (type Box [T] <struct (Value T)>)

  (fun <main> ()
    (block
      (var boxed <named Box [i64]>)
      (= boxed (cast (struct ((Value (: 10 i64)))) <named Box [i64]>))
      (output (call unwrap boxed) "\n")))

  (fun unwrap ((var text string)) -> i64
    (block
      (return (: 0 i64))))

  (fun unwrap [T] ((var box <named Box [T]>)) -> T
    (block
      (return (field box Value)))))
