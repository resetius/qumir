(block
  (type имя <struct (val i64)>)
  (fun <main> void () ()
    (block
      (var n <named имя>)
      (= n (call make_name 42))
      (output (call get_val (: n <named имя>)) "\n")))
  (fun make_name <named имя> ((var v i64)) ()
    (block
      (var $$return <named имя>)
      (= $$return (: (struct ((val v))) <named имя>))))
  (fun get_val i64 ((var n <named имя>)) ()
    (block
      (var $$return i64)
      (= $$return (field n val)))))
