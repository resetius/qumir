; module-fixture: imported by suffix.kum, not a standalone case
(block
  (type компл <struct (re f64) (im f64)>)
  (fun __imag ((var x f64)) -> компл (attrs (literal "i"))
    (block
      (return (: (struct ((re (: 0.0 f64)) (im x))) компл))))
  (fun Re ((var z компл)) -> f64
    (block
      (return (field z re))))
  (fun Im ((var z компл)) -> f64
    (block
      (return (field z im)))))
