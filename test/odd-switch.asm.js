function asmFunc(stdlib, foreign, heap) {
  'use asm';
  function func(x) {
    x = x | 0;
    switch (x | 0) {
      case (0): break; // parens around 0 are weird
    }
  }
  return { odd_switch: func };
}

