(block
  (pragma language overloads)

  (fun vector_print_element [T] ((var x T)) -> void
    (block (output x)))

  (fun vector_print_element ((var x u64)) -> void
    (block
      (if (>= x (: 10 u64)) (call vector_print_element (// x (: 10 u64))))
      (output (cast (+ (cast (% x (: 10 u64)) i64) 48) char))))

  (fun vector_print [T (const N i64)] ((var v <ref <vec T N>>)) -> void (attrs print)
    (block
      (output "{")
      (var i = 0)
      (while (< i N)
        (block
          (if (!= i 0) (output ", "))
          (call vector_print_element (index v i))
          (= i (+ i 1))))
      (output "}"))))
