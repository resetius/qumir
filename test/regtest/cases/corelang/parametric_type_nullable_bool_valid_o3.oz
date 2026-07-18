(block
  (pragma language overloads)
  (type Nullable [T] <struct (Value T) (Valid bool)>)

  (fun <main> ()
    (block
      (if (call cmp (: 50000.0 f64) #t)
        (output "111\n")
        (output "222\n"))))

  (fun cmp ((var v f64) (var valid bool)) -> bool
    (block
      (var nv <named Nullable [f64]>)
      (= nv (cast (struct ((Value v) (Valid valid))) <named Nullable [f64]>))
      (var r <named Nullable [bool]>)
      (= r (< nv 100000))
      (return (field r Valid))))

  (fun nullable_lt_rhs [T1 T2] ((var a <named Nullable [T1]>) (var b T2))
      -> <named Nullable [bool]> (attrs (operator "<"))
    (block
      (return (cast
        (if (field a Valid)
          (struct ((Value (< (field a Value) b)) (Valid #t)))
          (struct ((Valid #f))))
        <named Nullable [bool]>)))))
