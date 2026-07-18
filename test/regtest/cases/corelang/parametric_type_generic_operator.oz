(block
  (pragma language overloads)
  (type Box [T] <struct (Value T)>)

  (fun <main> ()
    (block
      (var a <named Box [i64]>)
      (var b <named Box [i64]>)
      (var c <named Box [i64]>)
      (= a (cast (: 4 i64) <named Box [i64]>))
      (= b (cast (: 8 i64) <named Box [i64]>))
      (= c (+ a b))
      (output (field c Value) "\n")))

  (fun box_from [T] ((var value T)) -> <named Box [T]> (attrs (operator "cast"))
    (block
      (return (cast (struct ((Value value))) <named Box [T]>))))

  (fun box_add [T] ((var a <named Box [T]>)
        (var b <named Box [T]>)) -> <named Box [T]> (attrs (operator "+"))
    (block
      (return (cast
        (struct ((Value (+ (field a Value) (field b Value)))))
        <named Box [T]>)))))
