(block
  (pragma language overloads)
  (type Nullable [T] <struct (Value T) (Valid bool)>)

  (fun <main> ()
    (block
      (var a <named Nullable [i64]>)
      (var b <named Nullable [f64]>)
      (var c <named Nullable [f64]>)
      (= a (cast (struct ((Value (: 4 i64)) (Valid #t))) <named Nullable [i64]>))
      (= b (cast (struct ((Value (: 0.5 f64)) (Valid #t))) <named Nullable [f64]>))
      (= c (* a b))
      (if (&& (field c Valid) (== (field c Value) (: 2.0 f64)))
        (output "ok\n")
        (output "bad\n"))))

  (fun nullable_mul [T1 T2 R] ((var a <named Nullable [T1]>)
        (var b <named Nullable [T2]>)) -> <named Nullable [R]> (attrs (operator "*"))
    (block
      (return (cast
        (if (&& (field a Valid) (field b Valid))
          (struct ((Value (* (field a Value) (field b Value))) (Valid #t)))
          (struct ((Valid #f))))
        <named Nullable [R]>)))))
