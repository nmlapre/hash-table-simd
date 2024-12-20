#include <array>
#include <cassert>
#include <chrono>
#include <unordered_set>
#include <vector>

#include "data.h"
#include "hash_set.h"

struct Timer {
  Timer(char const* message)
      : _message(message), _startTime(std::chrono::system_clock::now()) {}

  ~Timer() {
    auto const endTime = std::chrono::system_clock::now();
    auto const elapsed = endTime - _startTime;
    std::cout << _message << ": " << elapsed << "\n";
  }

  char const* _message;
  std::chrono::time_point<std::chrono::system_clock> _startTime;
};

std::vector<Data> GenerateDataset(size_t size) {
  std::vector<Data> result{};
  result.resize(size);
  for (size_t i = 0; i < size; ++i) {
    result[i] = {rand(), rand(), rand() / 3.14};
  }
  return result;
}

template <typename Container>
void RunTestCode(Container& container, std::vector<Data> const& values) {
  for (Data const& val : values) {
    container.insert(val);
  }
  for (Data const& val : values) {
    assert(container.contains(val));
  }
  for (Data const& val : values) {
    assert(container.erase(val));
    assert(!container.contains(val));
  }
}

// TODO: variations of testing:
//   - randomized insert/contains/erase
//   - larger data
//   - data with non-trivial destructor
template <typename Container>
void RandomizedTest(Container& container, std::vector<Data> const& values) {
  rand();
}

int main(int argc, char** argv) {
  size_t const datasetSize = std::stoi(argv[1]);
  auto const values = GenerateDataset(datasetSize);
  srand(time(0));

  // Flat HashSet implementation
  {
    Timer timer{"Flat HashSet implementation"};
    HashSet<Data> hs;
    RunTestCode(hs, values);
  }

  // std::unordered_set implementation
  {
    Timer timer{"std::unordered_set implementation"};
    std::unordered_set<Data> hs;
    RunTestCode(hs, values);
  }
  return 0;
}
