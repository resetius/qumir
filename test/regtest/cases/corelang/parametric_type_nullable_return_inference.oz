(block
  (pragma language overloads)
  (type Nullable [T] <struct (Value T) (Valid bool)>)

  (fun <main> ()
    (block
      (var a <named Nullable [i64]>)
      (var c <named Nullable [f64]>)
      (= a (cast (struct ((Value (: 4 i64)) (Valid #t))) <named Nullable [i64]>))
      (= c (+ a (: 0.5 f64)))
      (if (&& (field c Valid) (== (field c Value) (: 4.5 f64)))
        (output "ok\n")
        (output "bad\n"))))

  (fun nullable_add_rhs [T U R] ((var a <named Nullable [T]>) (var b U))
      -> <named Nullable [R]> (attrs (operator "+"))
    (block
      (return (cast
        (if (field a Valid)
          (struct ((Value (+ (field a Value) b)) (Valid #t)))
          (struct ((Valid #f))))
        <named Nullable [R]>)))))
