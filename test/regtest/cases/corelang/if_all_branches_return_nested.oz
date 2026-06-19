(block
  (fun <main> ()
    (block
      (output (call choose #t) " " (call choose #f) "\n")))

  (fun choose ((var condition bool)) -> i64
    (block
      (block
        (block
          (if condition
            (block
              (return (: 1 i64)))
            (block
              (return (: 0 i64)))))))))
