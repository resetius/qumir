(block
  (fun <main> ()
    (block
      (output (call checked_string) "\n")))

  (fun checked_string () -> string
    (attrs
      (expect_after (assert (== local "alive"))))
    (block
      (var local string)
      (= local "alive")
      (return local))))
