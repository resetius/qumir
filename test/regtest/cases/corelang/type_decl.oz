(block
  (type имя <struct (val i64)>)
  (fun <main> ()
    (block
      (var n <named имя>)
      (= n (call make_name 42))
      (output (call get_val (: n <named имя>)) "\n")))
  (fun make_name ((var v i64)) -> <named имя>
    (block
      (var знач <named имя>)
      (= знач (: (struct ((val v))) <named имя>))))
  (fun get_val ((var n <named имя>)) -> i64
    (block
      (var знач i64)
      (= знач (field n val)))))
