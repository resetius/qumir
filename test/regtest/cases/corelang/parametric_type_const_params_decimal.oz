(block
  (pragma language overloads)
  (type Decimal [(const Scale i32) (const Precision i32)] <struct (Value i64)>)

  (fun <main> ()
    (block
      (var a <named Decimal [2 10]>)
      (var b <named Decimal [2 10]>)
      (var c <named Decimal [2 10]>)
      (var d <named Decimal [2 10]>)
      (= a (cast (struct ((Value (: 40 i64)))) <named Decimal [2 10]>))
      (= b (+ a (: 2 i64)))
      (= c (+ b a))
      (= d (* c (: 2 i64)))
      (if (== (field d Value) (: 164 i64))
        (output "ok\n")
        (output "bad\n"))))

  (fun decimal_add_int [(const Scale i32) (const Precision i32)]
      ((var a <named Decimal [Scale Precision]>) (var b i64))
      -> <named Decimal [Scale Precision]> (attrs (operator "+"))
    (block
      (return (cast
        (struct ((Value (+ (field a Value) b))))
        <named Decimal [Scale Precision]>))))

  (fun decimal_add_decimal [(const Scale i32) (const Precision i32)]
      ((var a <named Decimal [Scale Precision]>) (var b <named Decimal [Scale Precision]>))
      -> <named Decimal [Scale Precision]> (attrs (operator "+"))
    (block
      (return (cast
        (struct ((Value (+ (field a Value) (field b Value)))))
        <named Decimal [Scale Precision]>))))

  (fun decimal_mul_int [(const Scale i32) (const Precision i32)]
      ((var a <named Decimal [Scale Precision]>) (var b i64))
      -> <named Decimal [Scale Precision]> (attrs (operator "*"))
    (block
      (return (cast
        (struct ((Value (* (field a Value) b))))
        <named Decimal [Scale Precision]>)))))
