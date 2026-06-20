(block
  (fun <main> ()
    (block
      (output (call checked_string) " " (call checked_string_early #t) "\n")))

  (fun checked_string () -> string
    (attrs
      (expect_after (assert (== local "alive"))))
    (block
      (var local string)
      (= local "alive")
      (return local)))

  (fun checked_string_early ((var condition bool)) -> string
    (attrs
      (expect_after (assert (== local "alive"))))
    (block
      (var local string)
      (= local "alive")
      (if condition
        (block
          (return local))
        (block
          (return local))))))
