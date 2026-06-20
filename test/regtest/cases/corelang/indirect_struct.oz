(block
  (fun <main> void () ()
    (block
      (var pair <struct (op <fun i64 (i64 i64)>) (tag i64)>)
      (= pair (: (struct ((op add) (tag (: 1 i64))))
        <struct (op <fun i64 (i64 i64)>) (tag i64)>))
      (output (field tag pair) ": " (call (field op pair) (: 7 i64) (: 8 i64)) "\n")
      (= pair (: (struct ((op mul) (tag (: 2 i64))))
        <struct (op <fun i64 (i64 i64)>) (tag i64)>))
      (output (field tag pair) ": " (call (field op pair) (: 7 i64) (: 8 i64)) "\n")))

  (fun add i64 ((var a i64) (var b i64)) ()
    (block
      (var $$return i64)
      (= $$return (+ a b))))

  (fun mul i64 ((var a i64) (var b i64)) ()
    (block
      (var $$return i64)
      (= $$return (* a b)))))
