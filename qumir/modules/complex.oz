(block
  (type компл <struct (re f64) (im f64)>)

  (fun complex_sqrt ((var x f64)) -> f64 (attrs (extern sqrt)) (block))
  (fun complex_atan2 ((var y f64) (var x f64)) -> f64 (attrs (extern atan2)) (block))

  (fun i () -> компл
    (block (return (: (struct ((re 0.0) (im 1.0))) компл))))
  (fun Re ((var z компл)) -> f64 (block (return (field z re))))
  (fun Im ((var z компл)) -> f64 (block (return (field z im))))

  (fun мод ((var z компл)) -> f64
    (block (return (call complex_sqrt (+ (* (field z re) (field z re)) (* (field z im) (field z im)))))))
  (fun аргумент ((var z компл)) -> f64
    (block (return (call complex_atan2 (field z im) (field z re)))))

  (fun сопряжённое ((var z компл)) -> компл
    (block (return (: (struct ((re (field z re)) (im (- (field z im))))) компл))))
  (fun сопряженное ((var z компл)) -> компл
    (block (return (call сопряжённое z))))

  (fun complex_add ((var a компл) (var b компл)) -> компл (attrs (operator "+"))
    (block (return (: (struct ((re (+ (field a re) (field b re))) (im (+ (field a im) (field b im))))) компл))))
  (fun complex_sub ((var a компл) (var b компл)) -> компл (attrs (operator "-"))
    (block (return (: (struct ((re (- (field a re) (field b re))) (im (- (field a im) (field b im))))) компл))))
  (fun complex_mul ((var a компл) (var b компл)) -> компл (attrs (operator "*"))
    (block (return (: (struct (
      (re (- (* (field a re) (field b re)) (* (field a im) (field b im))))
      (im (+ (* (field a re) (field b im)) (* (field a im) (field b re))))) ) компл))))
  (fun complex_div ((var a компл) (var b компл)) -> компл (attrs (operator "/"))
    (block
      (var d f64)
      (= d (+ (* (field b re) (field b re)) (* (field b im) (field b im))))
      (return (: (struct (
        (re (/ (+ (* (field a re) (field b re)) (* (field a im) (field b im))) d))
        (im (/ (- (* (field a im) (field b re)) (* (field a re) (field b im))) d))) ) компл))))
  (fun complex_neg ((var a компл)) -> компл (attrs (operator "neg"))
    (block (return (: (struct ((re (- (field a re))) (im (- (field a im))))) компл))))

  (fun complex_eq ((var a компл) (var b компл)) -> bool (attrs (operator "=="))
    (block (return (&& (== (field a re) (field b re)) (== (field a im) (field b im))))))
  (fun complex_ne ((var a компл) (var b компл)) -> bool (attrs (operator "!="))
    (block (return (|| (!= (field a re) (field b re)) (!= (field a im) (field b im))))))

  (fun __imag ((var x f64)) -> компл (attrs (literal "i"))
    (block (return (: (struct ((re 0.0) (im x))) компл))))

  (fun complex_from_float ((var x f64)) -> компл (attrs (operator "cast"))
    (block (return (: (struct ((re x) (im 0.0))) компл))))
  (fun complex_from_int ((var x i64)) -> компл (attrs (operator "cast"))
    (block (return (: (struct ((re (cast x f64)) (im 0.0))) компл))))
  (fun complex_to_float ((var z компл)) -> f64 (attrs (operator "cast"))
    (block (return (field z re))))
  (fun complex_to_int ((var z компл)) -> i64 (attrs (operator "cast"))
    (block (return (cast (field z re) i64))))

  (fun complex_print ((var z компл)) (attrs print)
    (block
      (output (field z re))
      (if (>= (field z im) 0.0) (output "+"))
      (output (field z im) "i"))))
