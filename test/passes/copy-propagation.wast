(module
  (func $multipass (param $0 i32) (param $1 i32) (param $2 i32) (result i32)
   (local $3 i32)
   (if
    (local.get $3)
    (local.set $3 ;; this set is completely removed, allowing later opts
     (i32.const 24)
    )
   )
   (if
    (local.get $3)
    (local.set $2
     (i32.const 0)
    )
   )
   (local.get $2)
  )
  (func $ssa-copies-multipass (param $p i32)
    (local $x i32)
    (local $y i32)
    (local $z i32)
    (local.set $x
      (local.get $p)
    )
    (local.set $y
      (local.tee $z
        (local.get $x)
      )
    )
    (call $ssa-copies-multipass (local.get $x))
    (call $ssa-copies-multipass (local.get $y))
    (call $ssa-copies-multipass (local.get $z))
  )
  (func $ssa-copies-multipass-b (result i32)
    (local $p i32)
    (local $x i32)
    (local $y i32)
    (local $z i32)
    (local.set $p
      (call $ssa-copies-multipass-b)
    )
    (local.set $x
      (local.get $p)
    )
    (local.set $y
      (local.tee $z
        (local.get $x)
      )
    )
    (call $ssa-copies-multipass (local.get $x))
    (call $ssa-copies-multipass (local.get $y))
    (call $ssa-copies-multipass (local.get $z))
    (i32.const 0)
  )
  (func $ssa-copies-multipass-c (result i32)
    (local $x i32)
    (local $y i32)
    (local $z i32)
    (local $p i32) ;; now $p is the last
    (local.set $p
      (call $ssa-copies-multipass-c)
    )
    (local.set $x
      (local.get $p)
    )
    (local.set $y
      (local.tee $z
        (local.get $x)
      )
    )
    (call $ssa-copies-multipass (local.get $x))
    (call $ssa-copies-multipass (local.get $y))
    (call $ssa-copies-multipass (local.get $z))
    (i32.const 0)
  )
  (func $ssa-copies-careful-here (param $0 i32)
   (local $1 i32)
   (if
    (loop $label$1 (result i32)
     (br_if $label$1
      (local.tee $0
       (local.tee $1
        (i32.load8_s offset=2
         (i32.const 0)
        )
       )
      )
     )
     (i32.const -1)
    )
    (drop
     (loop $label$3 (result f64)
      (local.set $1
       (i32.const 1)
      )
      (br_if $label$3
       (local.get $1)
      )
      (f64.const 0)
     )
    )
   )
  )
)
