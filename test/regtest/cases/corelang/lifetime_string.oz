(block
  (fun <main> ()
    (block
      (var local string)
      (= local "first")
      (= local (+ local "-assigned"))
      (output local "\n")
      (output (call surround local) "\n")
      (output (call make_string) "\n")))

  (fun surround ((var value string)) -> string
    (block
      (return (+ (+ "[" value) "]"))))

  (fun make_string () -> string
    (block
      (var result string)
      (= result "returned")
      (return result))))
