(block
  (use "Цвета")
  (type метка <struct (ц цвет) (н i64)>)
  (fun <main> ()
    (block
      (var м метка)
      (= м (: (struct ((ц (call красный)) (н (: 42 i64)))) метка))
      (output (field м н) "\n"))))
