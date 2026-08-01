(block
  (fun <main> ()
    (block
      (if (call choose #t)
        (output "bad")
        (output "ok"))
      (output " ")
      (if (call choose #f)
        (output "ok")
        (output "bad"))
      (output "\n")))

  (fun choose ((var condition bool)) -> bool
    (block
      (return (if condition #f #t)))))
