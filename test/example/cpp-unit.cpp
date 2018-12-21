// test multiple uses of the threadPool

#include <assert.h>

#include <wasm.h>
#include <ir/cost.h>

using namespace wasm;

void testCost() {
  // Some optimizations assume that the cost of a get is zero, e.g. local-cse.
  GetLocal get;
  assert(CostAnalyzer(&get).cost == 0);
}

void testSize() {
}

int main() 
{
  testCost();
  testSize();

  std::cout << "Success.\n";
  return 0;
}
