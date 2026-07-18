; disable_exec
(block
  (pragma language overloads)
  (type Nullable [T] <struct (Value T) (Valid bool)>)

  (fun <main> ()
    (block
      (var value <named Nullable [i32]>)
      (var rhs <named Nullable [i32]>)
      (var lhs <named Nullable [i32]>)
      (= value (cast (struct ((Value (: 7 i32)) (Valid #t))) <named Nullable [i32]>))
      (= rhs (+ value 1))
      (= lhs (+ 2 value))
      (output (call add_i32 1 (: 4 i32)) " ")
      (output (field rhs Value) " ")
      (output (field lhs Value) "\n")))

  (fun add_i32 ((var a i32) (var b i32)) -> i32
    (block
      (return (+ a b))))

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
      (return (+ (cast a <named Nullable [T]>) b)))))
