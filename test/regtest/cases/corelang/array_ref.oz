(block
  (fun <main> void () ()
    (block
      (var a <array i64 1> [0 2])
      (var b <array i64 2> [0 1] [0 1])
      (= a [0] (: 10 i64))
      (= a [1] (: 20 i64))
      (= a [2] (: 30 i64))
      (= b [0 0] (: 100 i64))
      (= b [0 1] (: 200 i64))
      (= b [1 0] (: 300 i64))
      (= b [1 1] (: 400 i64))
      (call bump (index a (: 1 i64)))
      (call bump2 (index b [(: 1 i64) (: 0 i64)]))
      (output
        (index a (: 0 i64))
        " "
        (index a (: 1 i64))
        " "
        (index a (: 2 i64))
        " "
        (index b [(: 1 i64) (: 0 i64)])
        "\n")))
  (fun bump void ((var x <ref i64>)) ()
    (block
      (= x (+ x (: 7 i64)))))
  (fun bump2 void ((var x <ref i64>)) ()
    (block
      (= x (+ x (: 11 i64))))))
