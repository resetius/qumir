(block
  (pragma language overloads)

  (fun tag ((var x i8)) -> i64
    (block
      (return (: 8 i64))))

  (fun tag ((var x u8)) -> i64
    (block
      (return (: 108 i64))))

  (fun tag ((var x i16)) -> i64
    (block
      (return (: 16 i64))))

  (fun tag ((var x u16)) -> i64
    (block
      (return (: 116 i64))))

  (fun tag ((var x i32)) -> i64
    (block
      (return (: 32 i64))))

  (fun tag ((var x u32)) -> i64
    (block
      (return (: 132 i64))))

  (fun tag ((var x i64)) -> i64
    (block
      (return (: 64 i64))))

  (fun tag ((var x u64)) -> i64
    (block
      (return (: 164 i64))))

  (fun <main> ()
    (block
      (output (call tag (: -1 i8)) "\n")
      (output (call tag (: 1 u8)) "\n")
      (output (call tag (: -1 i16)) "\n")
      (output (call tag (: 1 u16)) "\n")
      (output (call tag (: -1 i32)) "\n")
      (output (call tag (: 1 u32)) "\n")
      (output (call tag (: -1 i64)) "\n")
      (output (call tag (: 1 u64)) "\n"))))
