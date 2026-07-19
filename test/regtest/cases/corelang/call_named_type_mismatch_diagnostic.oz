; disable_exec
(block
  (type A i64)
  (type B f64)

  (fun <main> ()
    (block
      (var b <named B>)
      (= b (cast (: 1.0 f64) <named B>))
      (call accept b)))

  (fun accept ((var x <named A>)) -> void
    (block)))
