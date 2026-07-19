; disable_exec
(block
  (type Nullable [T] <struct (Value T) (Valid bool)>)

  (fun <main> ()
    (block
      (var nullable <named Nullable [f64]>)
      (var result f64)
      (= result (if #t
        (: 1.0 f64)
        nullable)))))
