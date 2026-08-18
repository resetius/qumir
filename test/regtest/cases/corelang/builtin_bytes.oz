(block
  (fun <main> ()
    (block
      (var src <ptr u8>)
      (= src (cast (call array_create (: 8 i64)) <ptr u8>))
      (var dst <ptr u8>)
      (= dst (cast (call array_create (: 8 i64)) <ptr u8>))
      (var i i64)
      (= i (: 0 i64))
      (while (< i (: 8 i64))
        (block
          (= src [i] (cast (+ i (: 10 i64)) u8))
          (= i (+ i (: 1 i64)))))
      (var copied <ptr u8>)
      (= copied (call builtin::memcpy dst src (: 8 i64)))
      (= i (: 0 i64))
      (while (< i (: 8 i64))
        (block
          (output (cast (index dst i) i64) " ")
          (= i (+ i (: 1 i64)))))
      (output "\n")
      (output (== copied dst) " "
              (== (call builtin::memcmp src dst (: 8 i64)) (: 0 i32)) "\n")

      (var buf <ptr u8>)
      (= buf (cast (call array_create (: 10 i64)) <ptr u8>))
      (= i (: 0 i64))
      (while (< i (: 10 i64))
        (block
          (= buf [i] (cast (+ i (: 1 i64)) u8))
          (= i (+ i (: 1 i64)))))
      (var srcPtr <ptr u8>)
      (= srcPtr buf)
      (var dstPtr <ptr u8>)
      (= dstPtr (cast (+ (cast buf i64) (: 2 i64)) <ptr u8>))
      (var moved <ptr u8>)
      (= moved (call builtin::memmove dstPtr srcPtr (: 8 i64)))
      (= i (: 0 i64))
      (while (< i (: 10 i64))
        (block
          (output (cast (index buf i) i64) " ")
          (= i (+ i (: 1 i64)))))
      (output "\n")
      (output (== moved dstPtr) "\n")

      (= dst [(: 0 i64)] (cast (: 99 i64) u8))
      (output (!= (call builtin::memcmp src dst (: 8 i64)) (: 0 i32)) "\n"))))
