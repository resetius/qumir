; disable_exec
(block
  (pragma language overloads)
  (type Nullable [T] <struct (Value T) (Valid bool)>)

  (fun <main> ()
    (block
      (var a <named Nullable [i32]>)
      (var b <named Nullable [i32]>)
      (var sum <named Nullable [i32]>)
      (var quotient <named Nullable [f64]>)
      (= a (cast (struct ((Value (: 9 i32)) (Valid #t))) <named Nullable [i32]>))
      (= b (cast (struct ((Value (: 3 i32)) (Valid #t))) <named Nullable [i32]>))
      (= sum (+ a b))
      (= quotient (/ a b))
      (output (field sum Value) " ")
      (output (field quotient Value) "\n")))

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
      (return (+ a (cast b <named Nullable [T]>)))))

  (fun nullable_add_lhs [T] ((var a T)
        (var b <named Nullable [T]>)) -> <named Nullable [T]> (attrs (operator "+"))
    (block
      (return (+ (cast a <named Nullable [T]>) b))))

  (fun nullable_div [T] ((var a <named Nullable [T]>)
        (var b <named Nullable [T]>)) -> <named Nullable [f64]> (attrs (operator "/"))
    (block
      (return (cast
        (if (&& (field a Valid) (field b Valid))
          (struct ((Value (/ (field a Value) (field b Value))) (Valid #t)))
          (struct ((Valid #f))))
        <named Nullable [f64]>))))

  (fun nullable_div_rhs [T] ((var a <named Nullable [T]>)
        (var b T)) -> <named Nullable [f64]> (attrs (operator "/"))
    (block
      (return (/ a (cast b <named Nullable [T]>)))))

  (fun nullable_div_lhs [T] ((var a T)
        (var b <named Nullable [T]>)) -> <named Nullable [f64]> (attrs (operator "/"))
    (block
      (return (/ (cast a <named Nullable [T]>) b)))))
