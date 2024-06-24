#include <boost/container_hash/hash.hpp>
#include <cinttypes>

struct Data {
  int x;
  int y;
  double z;

  bool operator==(Data const& other) const {
    return other.x == x && other.y == y && other.z == z;
  }

  size_t hash() const {
    size_t result = 0;
    boost::hash_combine(result, x);
    boost::hash_combine(result, y);
    boost::hash_combine(result, z);
    return result;
  }

  void print() const {
    printf("x: %d, y: %d, z: %lf", x, y, z);
  }
};

