(block
  (fun <main> ()
    (block
      (var arr <array <struct (lo i8) (mid i16) (wide i32) (huge i64)> 1> [0 1])
      (= arr [0]
        (: (struct ((lo (: 1 i8)) (mid (: 100 i16)) (wide (: 10000 i32)) (huge (: 1000000 i64))))
          <struct (lo i8) (mid i16) (wide i32) (huge i64)>))
      (= arr [1]
        (: (struct ((lo (: 2 i8)) (mid (: 200 i16)) (wide (: 20000 i32)) (huge (: 2000000 i64))))
          <struct (lo i8) (mid i16) (wide i32) (huge i64)>))
      (output
        "before idx1: "
        (cast (field (: (index arr (: 1 i64)) <struct (lo i8) (mid i16) (wide i32) (huge i64)>) lo) i64)
        " "
        (cast (field (: (index arr (: 1 i64)) <struct (lo i8) (mid i16) (wide i32) (huge i64)>) mid) i64)
        " "
        (cast (field (: (index arr (: 1 i64)) <struct (lo i8) (mid i16) (wide i32) (huge i64)>) wide) i64)
        " "
        (field (: (index arr (: 1 i64)) <struct (lo i8) (mid i16) (wide i32) (huge i64)>) huge)
        "\n")
      (call bump_rec (index arr (: 1 i64)))
      (output
        "after idx1: "
        (cast (field (: (index arr (: 1 i64)) <struct (lo i8) (mid i16) (wide i32) (huge i64)>) lo) i64)
        " "
        (cast (field (: (index arr (: 1 i64)) <struct (lo i8) (mid i16) (wide i32) (huge i64)>) mid) i64)
        " "
        (cast (field (: (index arr (: 1 i64)) <struct (lo i8) (mid i16) (wide i32) (huge i64)>) wide) i64)
        " "
        (field (: (index arr (: 1 i64)) <struct (lo i8) (mid i16) (wide i32) (huge i64)>) huge)
        "\n")))
  (fun bump_rec ((var p <ref <struct (lo i8) (mid i16) (wide i32) (huge i64)>>))
    (block
      (= p
        (: (struct
             ((lo (+ (field p lo) (: 10 i8)))
              (mid (+ (field p mid) (: 100 i16)))
              (wide (+ (field p wide) (: 1000 i32)))
              (huge (+ (field p huge) (: 10000 i64)))))
          <struct (lo i8) (mid i16) (wide i32) (huge i64)>)))))
