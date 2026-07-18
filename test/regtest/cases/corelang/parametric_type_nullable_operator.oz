(block
  (pragma language overloads)
  (type Nullable [T] <struct (Value T) (Valid bool)>)

  (fun <main> ()
    (block
      (var a <named Nullable [i64]>)
      (var b <named Nullable [i64]>)
      (var bad <named Nullable [i64]>)
      (var c <named Nullable [i64]>)
      (= a (cast (struct ((Value (: 4 i64)) (Valid #t))) <named Nullable [i64]>))
      (= b (cast (struct ((Value (: 8 i64)) (Valid #t))) <named Nullable [i64]>))
      (= bad (cast (struct ((Valid #f))) <named Nullable [i64]>))
      (= c (+ a b))
      (output (field c Value) " ")
      (if (field c Valid) (output "valid ") (output "null "))
      (= c (+ a bad))
      (output (field c Value) " ")
      (if (field c Valid) (output "valid\n") (output "null\n"))))

  (fun nullable_add [T] ((var a <named Nullable [T]>)
        (var b <named Nullable [T]>)) -> <named Nullable [T]> (attrs (operator "+"))
    (block
      (return (cast
        (if (&& (field a Valid) (field b Valid))
          (struct ((Value (+ (field a Value) (field b Value))) (Valid #t)))
          (struct ((Valid #f))))
        <named Nullable [T]>)))))
