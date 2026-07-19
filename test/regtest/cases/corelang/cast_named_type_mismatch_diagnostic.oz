; disable_exec
(block
  (type A i64)
  (type B f64)

  (fun <main> ()
    (block
      (var a <named A>)
      (var b <named B>)
      (= a (cast (: 1 i64) <named A>))
      (= b (cast a <named B>)))))
