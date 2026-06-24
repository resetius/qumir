; module-fixture: imported by use_module.oz, not a standalone case
(block
  (fun add ((var a i64) (var b i64)) -> i64
    (block
      (return (+ a b)))))
