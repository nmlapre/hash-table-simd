#include "data.h"
#include "hash_set.h"
#include <cassert>

int main() {
  HashSet<Data> hs;
  const Data values[] = {
    {2,3,4.0},
    {2,3,4.1},
    {2,3,4.2},
    {2,3,4.3},
    {2,3,4.4},

    {3,3,4.0},
    {4,3,4.1},
    {5,3,4.2},
    {6,3,4.3},
    {7,3,4.4},
    {8,3,4.0},
    {9,3,4.1},
    {0,3,4.2},
    {10,3,4.3},
    {11,3,4.4},
    {12,3,4.4},
    {13,3,4.4},
    {14,3,4.4},
    {15,3,4.4},
    {16,3,4.4},
    {17,3,4.4},
    {18,3,4.4},
  };
  for (const Data& val : values) {
    hs._insert(val);
    hs._print();
  }
  for (const Data& val : values) {
    assert(hs._contains(val));
  }
  for (const Data& val : values) {
    hs._remove(val);
    hs._print();
    assert(!hs._contains(val));
  }
  return 0;
}
