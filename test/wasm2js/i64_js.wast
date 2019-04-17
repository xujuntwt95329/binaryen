(module
 (func "foo64" (param $x i64) (result i64)
  (local.set $x
   (i64.add
    (local.get $x)
    (i64.const 0x11223344ABCDEFAA)
   )
  )
  (local.get $x)
 )
)
(;;
(module
 (import "env" "bar64" (func $bar64 (param i64) (result i64)))
 (func "foo64" (param $x i64) (result i64)
  (local.set $x
   (i64.add
    (local.get $x)
    (call $bar64
     (i64.const 0x11223344ABCDEFAA)
    )
   )
  )
  (local.get $x)
 )
)
;;)
