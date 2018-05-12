;; a testcase that requires late dce
(module
 (type $0 (func (param f64 f64) (result f64)))
 (global $global$0 (mut i32) (i32.const 10))
 (export "func_4" (func $0))
 (func $0 (; 0 ;) (type $0) (param $var$0 f64) (param $var$1 f64) (result f64)
  (local $var$2 i32)
  (if (result f64)
   (tee_local $var$2
    (i32.const -4194304)
   )
   (block (result f64)
    (set_global $global$0
     (i32.const -1)
    )
    (loop $label$2
     (loop $label$3
      (br_if $label$3
       (block $label$4 (result i32)
        (set_global $global$0
         (i32.const 0)
        )
        (i32.const 1)
       )
      )
      (set_local $var$0
       (if (result f64)
        (block $label$5 (result i32)
         (set_global $global$0
          (i32.const -1)
         )
         (i32.const 0)
        )
        (block (result f64)
         (br_if $label$2
          (get_local $var$2)
         )
         (f64.const 4096)
        )
        (f64.const 1)
       )
      )
     )
     (return
      (get_local $var$0)
     )
    )
   )
   (f64.const 1)
  )
 )
)
