; disable_exec
(block
  (fun bitoff ((var bitmap <ptr u8>) (var row i64) (var off i64)) -> bool
    (block
      (return #t)))

  (fun touch ((var p <ptr u8>)) -> <ptr u8>
    (block
      (return p)))

  (fun test ((var bitmap <ptr u8>) (var row i64)) -> bool
    (block
      (var valid bool)
      (var p <ptr u8>)

      (= valid (call bitoff bitmap row (: 0 i64)))
      (= p (call touch bitmap))

      (return (&& #t valid)))))