(block
  (var prefix = "global")
  (var numbers <array i64 1> [1 2])
  (var words <array string 1> [1 2])

  (fun <main> ()
    (block
      (= numbers [1] (: 10 i64))
      (= numbers [2] (: 20 i64))
      (= words [1] "one")
      (= words [2] "two")
      (output
        prefix
        " "
        (index numbers (: 1 i64))
        " "
        (index numbers (: 2 i64))
        " "
        (index words (: 1 i64))
        " "
        (index words (: 2 i64))
        "\n"))))
