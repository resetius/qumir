(block
  (fun <main> ()
    (block
      (output
        (call choose (: 0 i64)) " "
        (call choose (: 1 i64)) " "
        (call choose (: 2 i64)) " "
        (call choose (: 3 i64)) " "
        (call choose (: 4 i64)) "\n")))

  (fun choose ((var value i64)) -> i64
    (block
      (if (== value (: 0 i64))
        (block
          (return (: 10 i64)))
        (if (== value (: 1 i64))
          (block
            (return (: 20 i64)))
          (if (== value (: 2 i64))
            (block
              (return (: 30 i64)))
            (if (== value (: 3 i64))
              (block
                (return (: 40 i64)))
              (block
                (return (: 50 i64))))))))))
