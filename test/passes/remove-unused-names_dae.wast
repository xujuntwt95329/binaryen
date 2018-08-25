(module
  (func $c5 (result i32)
    (block $x (result i32)
      (return (i32.const 8))
    )
  )
  (func $d5
    (drop (call $c5))
  )
  (func $c6 (result i32)
    (block $x (result i32)
      (i32.const 9)
    )
  )
  (func $d6
    (drop (call $c6))
  )
)

