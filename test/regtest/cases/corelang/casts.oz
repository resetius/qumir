(block
  (fun <main> void () ()
    (block
      (var u u32)
      (var i i16)
      (= u (: 4000000000 u32))
      (= i (cast (: 255 u8) i16))
      (output
        (+ u (: 1 u32))
        " "
        i
        " "
        (< (: 4000000000 u32) (: 1 u32))
        " "
        (< (: -1 i64) (: 1 i64))
        " "
        (cast (: 12.75 f64) u16)
        " "
        (cast (: 255 u8) f64)
        "\n"))))
