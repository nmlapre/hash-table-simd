#include <iostream>
#include <vector>
#include <utility>
#include <optional>
#include <memory>
#include <cinttypes>
#include <boost/tti/has_member_function.hpp>

BOOST_TTI_HAS_MEMBER_FUNCTION(print);

enum class Control : uint8_t {
  Empty   = 0b1111'1111,
  Removed = 0b1000'0000,
  // Full = 0b0...'....
};

template<typename V>
struct HashSet {

  static constexpr size_t GroupSize = 16;

  HashSet(size_t initialCapacity = 4)
    : _count(0)
  {
    _groupCount = 1;
    const size_t size = _groupCount * GroupSize + _groupCount * sizeof(V) * GroupSize;
    _data = std::make_unique<std::byte[]>(size);
    std::memset(_data.get(), 0xFF, GroupSize);
  }

  bool insert(V v) {
    const bool inserted = _insert(std::move(v), _data);
    if (inserted) {
      _count++;
    }
    if (_count > size_t(_groupCount * GroupSize * 0.8)) {
      rehash();
    }
    return inserted;
  }

  bool contains(V const& v) const {
    const size_t hash = v.hash();
    const uint8_t mostSignificantBits = uint8_t(hash >> 57);
    const Control ctrl{mostSignificantBits};
    size_t groupIndex = v.hash() % _groupCount;
    const size_t initialGroupIndex = groupIndex;
    while (true) {
      for (size_t i = 0; i < GroupSize; ++i) {
        if (Control{_data[groupIndex * GroupSize + i]} == ctrl &&
            *reinterpret_cast<V*>(&_data[
              _groupCount * GroupSize +              // metadata
              groupIndex * GroupSize * sizeof(V) +   // relevant group in values
              sizeof(V) * i])                        // relevant entry in value group
            == v) {
          return true;
        }
      }
      groupIndex = (groupIndex + 1) % _groupCount;
      if (groupIndex == initialGroupIndex) {
        return false;
      }
    }
    return false;
  }

  bool remove(V const& v) {
    const size_t hash = v.hash();
    const uint8_t mostSignificantBits = uint8_t(hash >> 57);
    const Control ctrl{mostSignificantBits};
    size_t groupIndex = v.hash() % _groupCount;
    const size_t initialGroupIndex = groupIndex;
    while (true) {
      for (size_t i = 0; i < GroupSize; ++i) {
        if (Control{_data[groupIndex * GroupSize + i]} == ctrl &&
            *reinterpret_cast<V*>(&_data[
              _groupCount * GroupSize +              // metadata
              groupIndex * GroupSize * sizeof(V) +   // relevant group in values
              sizeof(V) * i])                        // relevant entry in value group
            == v) {
          // remove it
          _data[groupIndex * GroupSize + i] = std::byte{Control::Removed};
          _count--;
          // note: don't actually have to do anything to the entry
          // could memzero it in debug?
          return true;
        }
      }
      groupIndex = (groupIndex + 1) % _groupCount;
      if (groupIndex == initialGroupIndex) {
        return false;
      }
    }
    return false;
  }

  void print() const {
    printf("Printing contents of hash table:\n");
    printf("group count: %zu, entry count: %zu\n", _groupCount, _count);
    size_t i = 0;
    printf("Printing metadata:\n");
    for (; i < GroupSize * _groupCount; ++i)
    {
      if (i % GroupSize == 0) {
        printf("Group %zu:\n", i / GroupSize);
      }
      const bool hasValue = !bool(_data[i] & std::byte(0b1000'0000));
      std::cout << "index: " << i % GroupSize << " -- " << hasValue << " : ";
      if (hasValue) {
        if constexpr (HasPrint) {
          reinterpret_cast<V*>(&_data[
            _groupCount * GroupSize + // skip metadata
            i * sizeof(V)             // skip to index
          ])->print();
        } else {
          std::cout << "[no print function]";
        }
      }
      std::cout << std::endl;
    }
  }

private:
  size_t _count;
  std::unique_ptr<std::byte[]> _data;
  size_t _groupCount;

  static constexpr bool HasPrint =
    has_member_function_print<V const, void>::value;

  void rehash() {
    _groupCount++;
    const size_t size = _groupCount * GroupSize + _groupCount * sizeof(V) * GroupSize;
    std::unique_ptr<std::byte[]> newData = std::make_unique<std::byte[]>(size);
    std::memset(newData.get(), 0xFF, _groupCount * GroupSize);

    // walk through all of the metadata and rehash to new data
    const size_t prevGroupCount = _groupCount - 1;
    for (size_t i = 0; i < prevGroupCount * GroupSize; ++i) {
      if (uint8_t(_data[i]) & 0b1000'0000) {
        continue;
      }
      // TODO: std::launder?
      V* value = reinterpret_cast<V*>(&_data[prevGroupCount * GroupSize + sizeof(V) * i]);
      _insert(std::move(*value), newData);
    }
    _data = std::move(newData);
  }

  bool _insert(V v, std::unique_ptr<std::byte[]>& data) {
    const size_t hash = v.hash();
    const uint8_t mostSignificantBits = uint8_t(hash >> 57);
    const Control ctrl{mostSignificantBits};
    size_t groupIndex = v.hash() % _groupCount;
    const size_t initialGroupIndex = groupIndex;
    while (true) {
      for (size_t i = 0; i < GroupSize; ++i) {
        if (uint8_t(data[groupIndex * GroupSize + i]) & 0b1000'0000) {
          data[groupIndex * GroupSize + i] = std::byte{ctrl};
          V* slot = reinterpret_cast<V*>(&data[
              _groupCount * GroupSize +
              groupIndex * GroupSize * sizeof(V) +
              sizeof(V) * i
          ]);
          *slot = std::move(v);
          return true;
        }
      }
      groupIndex = (groupIndex + 1) % _groupCount;
      if (groupIndex == initialGroupIndex) {
        return false;
      }
    }
    return false;
  } 
};

