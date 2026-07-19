; disable_exec
(block
  (pragma language overloads)
  (type StringView <struct (Ptr <ptr i8>) (Size i64)>)
  (type Nullable [T] <struct (Value T) (Valid bool)>)

  (fun <main> ()
    (block
      (call substr (: 1 i64) (: 0 i32) (: 1 i32))))

  (fun substr ((var str StringView) (var start i32) (var length i32)) -> StringView
    (block
      (return str)))

  (fun substr ((var str <named Nullable [StringView]>) (var start i32) (var length i32))
      -> <named Nullable [StringView]>
    (block
      (return str))))
