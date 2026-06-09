(block
  (fun <main> ()
    (block
      (var grid <array <struct (lo i8) (mid i16) (wide i32) (huge i64)> 2> [0 1] [0 1])
      (= grid [0 0]
        (: (struct ((lo (: 3 i8)) (mid (: 30 i16)) (wide (: 300 i32)) (huge (: 3000 i64))))
          <struct (lo i8) (mid i16) (wide i32) (huge i64)>))
      (= grid [0 1]
        (: (struct ((lo (: 4 i8)) (mid (: 40 i16)) (wide (: 400 i32)) (huge (: 4000 i64))))
          <struct (lo i8) (mid i16) (wide i32) (huge i64)>))
      (= grid [1 0]
        (: (struct ((lo (: 5 i8)) (mid (: 50 i16)) (wide (: 500 i32)) (huge (: 5000 i64))))
          <struct (lo i8) (mid i16) (wide i32) (huge i64)>))
      (= grid [1 1]
        (: (struct ((lo (: 6 i8)) (mid (: 60 i16)) (wide (: 600 i32)) (huge (: 6000 i64))))
          <struct (lo i8) (mid i16) (wide i32) (huge i64)>))
      (output
        "before idx[1 0]: "
        (cast (field (: (index grid [(: 1 i64) (: 0 i64)]) <struct (lo i8) (mid i16) (wide i32) (huge i64)>) lo) i64)
        " "
        (cast (field (: (index grid [(: 1 i64) (: 0 i64)]) <struct (lo i8) (mid i16) (wide i32) (huge i64)>) mid) i64)
        " "
        (cast (field (: (index grid [(: 1 i64) (: 0 i64)]) <struct (lo i8) (mid i16) (wide i32) (huge i64)>) wide) i64)
        " "
        (field (: (index grid [(: 1 i64) (: 0 i64)]) <struct (lo i8) (mid i16) (wide i32) (huge i64)>) huge)
        "\n")
      (call bump_rec (index grid [(: 1 i64) (: 0 i64)]))
      (output
        "after idx[1 0]: "
        (cast (field (: (index grid [(: 1 i64) (: 0 i64)]) <struct (lo i8) (mid i16) (wide i32) (huge i64)>) lo) i64)
        " "
        (cast (field (: (index grid [(: 1 i64) (: 0 i64)]) <struct (lo i8) (mid i16) (wide i32) (huge i64)>) mid) i64)
        " "
        (cast (field (: (index grid [(: 1 i64) (: 0 i64)]) <struct (lo i8) (mid i16) (wide i32) (huge i64)>) wide) i64)
        " "
        (field (: (index grid [(: 1 i64) (: 0 i64)]) <struct (lo i8) (mid i16) (wide i32) (huge i64)>) huge)
        "\n")))
  (fun bump_rec ((var p <ref <struct (lo i8) (mid i16) (wide i32) (huge i64)>>))
    (block
      (= p
        (: (struct
             ((lo (+ (field p lo) (: 1 i8)))
              (mid (+ (field p mid) (: 2 i16)))
              (wide (+ (field p wide) (: 3 i32)))
              (huge (+ (field p huge) (: 4 i64)))))
          <struct (lo i8) (mid i16) (wide i32) (huge i64)>)))))
