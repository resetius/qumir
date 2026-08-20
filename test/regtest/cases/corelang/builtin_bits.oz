(block
  (fun <main> ()
    (block
      ;; cttz: 0 is defined and yields 64
      (output (call builtin::cttz (: 1 u64)) " "
              (call builtin::cttz (: 8 u64)) " "
              (call builtin::cttz (: 128 u64)) " "
              (call builtin::cttz (<< (: 1 u64) (: 63 u64))) " "
              (call builtin::cttz (: 0 u64)) "\n")

      ;; ctlz: same zero contract
      (output (call builtin::ctlz (: 1 u64)) " "
              (call builtin::ctlz (: 128 u64)) " "
              (call builtin::ctlz (<< (: 1 u64) (: 63 u64))) " "
              (call builtin::ctlz (: 0 u64)) "\n")

      (output (call builtin::ctpop (: 0 u64)) " "
              (call builtin::ctpop (: 255 u64)) " "
              (call builtin::ctpop (: 1 u64)) "\n")

      ;; The SwissTable use: a group mask carries bits only at 8k+7, so
      ;; cttz >> 3 recovers the matching slot index within the group.
      (var k i64)
      (= k (: 0 i64))
      (while (< k (: 8 i64))
        (block
          (var mask = (<< (: 128 u64) (* (cast k u64) (: 8 u64))))
          (output (>> (call builtin::cttz mask) (: 3 i64)) " ")
          (= k (+ k (: 1 i64)))))
      (output "\n"))))
