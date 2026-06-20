(block
  (fun <main> ()
    (block
      (var lower = (: 1 i64))
      (var upper = (: 2 i64))
      (var words <array string 1> [lower upper])
      (= words [1] "one")
      (= words [2] "two")
      (= upper (: 100 i64))

      (var empty <array string 1> [1 0])

      (var grid <array string 2> [0 1] [2 3])
      (= grid [0 2] "a")
      (= grid [0 3] "b")
      (= grid [1 2] "c")
      (= grid [1 3] "d")

      (output
        (index words (: 1 i64))
        " "
        (index words (: 2 i64))
        " "
        (index grid [(: 0 i64) (: 2 i64)])
        (index grid [(: 0 i64) (: 3 i64)])
        (index grid [(: 1 i64) (: 2 i64)])
        (index grid [(: 1 i64) (: 3 i64)])
        "\n"))))
