(block
  (pragma language overloads)
  (type Nullable [T] <struct (Value T) (Valid bool)>)

  (fun <main> ()
    (block
      (var value <named Nullable [f64]>)
      (var result <named Nullable [f64]>)
      (= value (cast (struct ((Value (: 2.5 f64)) (Valid #t))) <named Nullable [f64]>))
      (= result (+ value (: 1 i64)))
      (output (field result Value) "\n")))

  (fun nullable_from_value [T] ((var value T)) -> <named Nullable [T]> (attrs (operator "cast"))
    (block
      (return (cast (struct ((Value value) (Valid #t))) <named Nullable [T]>))))

  (fun nullable_add [T] ((var a <named Nullable [T]>)
        (var b <named Nullable [T]>)) -> <named Nullable [T]> (attrs (operator "+"))
    (block
      (return (cast
        (if (&& (field a Valid) (field b Valid))
          (struct ((Value (+ (field a Value) (field b Value))) (Valid #t)))
          (struct ((Valid #f))))
        <named Nullable [T]>))))

  (fun nullable_add_rhs [T] ((var a <named Nullable [T]>)
        (var b T)) -> <named Nullable [T]> (attrs (operator "+"))
    (block
      (return (+ a (cast b <named Nullable [T]>))))))
