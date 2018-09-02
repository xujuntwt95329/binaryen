function M(stdlib, foreign, heap) {
  'use asm';
  function the_one() {
    return 0;
  }
  return the_one; // returned without an enclosing object
}

