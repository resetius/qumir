(block
  (pragma language overloads)
  (type Nullable [T] <struct (Value T) (Valid bool)>)
  (type null_tag <struct (Tag i8)>)

  (fun <main> ()
    (block
      (var int_null <named Nullable [i64]>)
      (var float_null <named Nullable [f64]>)
      (= int_null (cast (call null) <named Nullable [i64]>))
      (= float_null (cast (call null) <named Nullable [f64]>))
      (if (field int_null Valid) (output "i64-valid ") (output "i64-null "))
      (if (field float_null Valid) (output "f64-valid\n") (output "f64-null\n"))))

  (fun null () -> null_tag
    (block
      (return (cast (struct ((Tag (: 0 i8)))) null_tag))))

  (fun nullable_from_null [T] ((var value null_tag)) -> <named Nullable [T]> (attrs (operator "cast"))
    (block
      (return (cast (struct ((Valid #f))) <named Nullable [T]>)))))
