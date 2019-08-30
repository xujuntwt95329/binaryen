(module
  (memory 1 2)
  (import "env" "import" (func $import))
  (func $calls-import
    (call $import)
  )
)

