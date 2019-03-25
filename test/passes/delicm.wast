(module
  (func $a
    (local $x i32)
    (local.set $x (i32.const 0))
    (loop $l
      (drop (local.get $x))
      (br_if $l (i32.const 1))
    )
  )
  (func $no-tee
    (local $x i32)
    (drop
      (local.tee $x (i32.const 0))
    )
    (loop $l
      (drop (local.get $x))
      (br_if $l (i32.const 1))
    )
  )
  (func $no-multiget
    (local $x i32)
    (local.set $x (i32.const 0))
    (loop $l
      (drop (local.get $x))
      (drop (local.get $x))
      (br_if $l (i32.const 1))
    )
  )
)

