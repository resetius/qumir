(block
  (fun <main> ()
    (block
      (var base i128)
      (= base (* (: 1000000000000 i128) (: 1000000000 i128)))

      (var v <struct (tag i32) (wide i128) (tail i64)>)
      (= v (: (struct ((tag (: 7 i32)) (wide base) (tail (: 42 i64))))
            <struct (tag i32) (wide i128) (tail i64)>))

      (output "in:   " (cast (field v tag) i64)
        " " (cast (>> (field v wide) (: 64 i128)) i64)
        " " (cast (field v wide) i64)
        " " (field v tail) "\n")

      ; by value into a function and back out as a return value
      (var r <struct (tag i32) (wide i128) (tail i64)>)
      (= r (call bump v))
      (output "ret:  " (cast (field r tag) i64)
        " " (cast (>> (field r wide) (: 64 i128)) i64)
        " " (cast (field r wide) i64)
        " " (field r tail) "\n")

      ; by reference: the callee must write all 128 bits back
      (call scale v)
      (output "ref:  " (cast (field v tag) i64)
        " " (cast (>> (field v wide) (: 64 i128)) i64)
        " " (cast (field v wide) i64)
        " " (field v tail) "\n")

      ; a bare i128 argument and a bare i128 return
      (output "plain:" (cast (>> (call twice base) (: 64 i128)) i64)
        " " (cast (call twice base) i64) "\n")))

  (fun bump ((var s <struct (tag i32) (wide i128) (tail i64)>)) -> <struct (tag i32) (wide i128) (tail i64)>
    (block
      (return (: (struct
                   ((tag (+ (field s tag) (: 1 i32)))
                    (wide (+ (field s wide) (field s wide)))
                    (tail (+ (field s tail) (: 1 i64)))))
              <struct (tag i32) (wide i128) (tail i64)>))))

  (fun scale ((var p <ref <struct (tag i32) (wide i128) (tail i64)>>))
    (block
      (= p (: (struct
                ((tag (field p tag))
                 (wide (* (field p wide) (: 3 i128)))
                 (tail (field p tail))))
           <struct (tag i32) (wide i128) (tail i64)>))))

  (fun twice ((var x i128)) -> i128
    (block
      (return (+ x x)))))
