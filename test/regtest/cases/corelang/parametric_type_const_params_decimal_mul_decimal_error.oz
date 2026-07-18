; disable_exec
(block
  (pragma language overloads)
  (type Decimal [(const Scale i32) (const Precision i32)] <struct (Value i64)>)

  (fun <main> ()
    (block
      (var a <named Decimal [2 10]>)
      (var b <named Decimal [2 10]>)
      (var c <named Decimal [2 10]>)
      (= a (cast (struct ((Value (: 2 i64)))) <named Decimal [2 10]>))
      (= b (cast (struct ((Value (: 3 i64)))) <named Decimal [2 10]>))
      (= c (* a b))))

  (fun decimal_mul_int [(const Scale i32) (const Precision i32)]
      ((var a <named Decimal [Scale Precision]>) (var b i64))
      -> <named Decimal [Scale Precision]> (attrs (operator "*"))
    (block
      (return (cast
        (struct ((Value (* (field a Value) b))))
        <named Decimal [Scale Precision]>)))))
