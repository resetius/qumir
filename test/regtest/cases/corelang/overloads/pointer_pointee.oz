; disable_exec
(block
  (pragma language overloads)
  (type A <struct (x i64)>)
  (type B <struct (x i64) (valid bool)>)

  (fun witness_value ((var witness <ptr <named A>>)) -> i64 (attrs cacheable)
    (block
      (return 20)))
  (fun witness_value ((var witness <ptr <named B>>)) -> i64 (attrs cacheable)
    (block
      (return 22)))

  (fun <main> ()
    (block
      (var value i64)
      (= value (+
        (call witness_value (cast 0 <ptr <named A>>))
        (call witness_value (cast 0 <ptr <named B>>)))))))
