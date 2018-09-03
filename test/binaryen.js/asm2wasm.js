var module = Binaryen.asm2wasm(`
  function asmFunc(stdlib, foreign, heap) {
    'use asm';
    function func(x) {
      x = x | 0;
      return x + 1 | 0;
    }
    return { add1: func };
  }
`);

console.log(module.emitText());

