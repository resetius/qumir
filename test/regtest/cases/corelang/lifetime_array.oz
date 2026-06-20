(block
  (fun <main> ()
    (block
      (var numbers <array i64 1> [1 3])
      (= numbers [1] (: 10 i64))
      (= numbers [2] (: 20 i64))
      (= numbers [3] (: 30 i64))

      (var words <array string 1> [1 2])
      (= words [1] "one")
      (= words [2] "two")

      (output
        (index numbers (: 1 i64))
        " "
        (index numbers (: 2 i64))
        " "
        (index numbers (: 3 i64))
        "\n")
      (output
        (index words (: 1 i64))
        " "
        (index words (: 2 i64))
        "\n"))))
