(module
 (func $0 (param $var$0 i32) (param $var$1 f32) (param $var$2 i32) (param $var$3 f64) (param $var$4 f64)
  (loop $label$1
   (if
    (i32.const 1)
    (block
     (set_local $var$2
      (block (result i32)
       (block $label$4
        (block
         (loop $label$6
          (nop)
         )
         (unreachable)
        )
       )
       (i32.const 1)
      )
     )
     (nop)
    )
   )
   (br_if $label$1
    (get_local $var$2)
   )
  )
 )
)

