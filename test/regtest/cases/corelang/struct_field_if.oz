(block
  (type StringView <struct (Data <ptr u8>) (Size i64)>)
  (type Nullable [T] <struct (Value T) (Valid bool)>)

  (fun <main> ()
    (block
      (var lit <named StringView>)
      (= lit (: (struct ((Data (cast 0 <ptr u8>)) (Size (: 0 i64)))) <named StringView>))
      (var nx <named Nullable [<named StringView>]>)
      (= nx (: (struct ((Value lit) (Valid #t))) <named Nullable [<named StringView>]>))
      (var result <named StringView>)
      (= result (if (field nx Valid)
        (field nx Value)
        lit))
      (if (== (field result Size) (: 0 i64))
        (output "ok\n")
        (output "bad\n")))))
