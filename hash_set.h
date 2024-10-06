#include <iostream>
#include <vector>
#include <utility>
#include <optional>
#include <memory>
#include <cinttypes>
#include <boost/tti/has_member_function.hpp>
#include <bit>

#ifdef __x86_64__
  #include <immintrin.h>
#else
  #include "sse2neon.h"
#endif

BOOST_TTI_HAS_MEMBER_FUNCTION(print);

enum class Control : uint8_t {
  Empty   = 0b1111'1111,
  Removed = 0b1000'0000,
  // Full = 0b0...'....
};

template<typename V, size_t GrowthFactor = 2>
struct HashSet {

  static constexpr size_t GroupSize = 16;

  HashSet(size_t initialCapacity = 4)
    : _count(0)
    , _groupCount(1)
    , _data(std::make_unique<std::byte[]>(
          (_groupCount * GroupSize) * (1 + sizeof(V))
       /* [        capacity       ]   [control+value] */
        ))
  {
    std::memset(_data.get(), 0xFF, GroupSize);
  }

  bool insert(V v) {
    if (_count > size_t(_groupCount * GroupSize * 0.8)) {
      _rehash();
    }
    const bool inserted = _insert(std::move(v), _data);
    assert(inserted);
    _count++;
    return inserted;
  }

  bool contains(V const& v) const {
    Control* ctrl;
    V* entry;
    return _find(v, ctrl, entry);
  }

  bool erase(V const& v) {
    Control* ctrl;
    V* entry;
    const bool found = _find(v, ctrl, entry);
    if (!found) {
      return false;
    }

    _count--;
    *ctrl = Control::Removed;

    // We don't actually have to do anything to the erased entry if it's
    // trivially destructible. Otherwise, run the destructor.
    if constexpr (!std::is_trivially_destructible_v<V>) {
      entry->~V();
    }

#if DEBUG
    // Zero memory out in debug just for debugging help.
    memset(entry, 0x00, sizeof(V));
#endif
    return true;
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
  size_t _groupCount;
  std::unique_ptr<std::byte[]> _data;

  static constexpr bool HasPrint =
    has_member_function_print<V const, void>::value;

  void _rehash() {
    const size_t prevGroupCount = _groupCount;
    _groupCount *= GrowthFactor;
    const size_t size = _groupCount * GroupSize + _groupCount * sizeof(V) * GroupSize;
    std::unique_ptr<std::byte[]> newData = std::make_unique<std::byte[]>(size);
    std::memset(newData.get(), 0xFF, _groupCount * GroupSize);

    // walk through metadata 16 slots at a time
    for (size_t groupIndex = 0; groupIndex < prevGroupCount; ++groupIndex) {
      // get the 16 byte chunk to examine
      void* group = _data.get() + (groupIndex * GroupSize);
      __m128i groupVec = _mm_loadu_si128(reinterpret_cast<__m128i*>(group));
      // broadcast the single-byte control sequence to a 16-byte vector
      __m128i ctrlVec = _mm_set1_epi8(uint8_t(0b1000'0000));
      // AND each byte with the ctrlVec to discard all but the interesting high bit
      __m128i maskedVec = _mm_and_si128(groupVec, ctrlVec);
      // check whether each byte equals 0x00
      // this verifies that the original highest bit was 0
      __m128i zero = _mm_setzero_si128();
      __m128i cmpVec = _mm_cmpeq_epi8(maskedVec, zero);
      // get the position of matching bytes
      // [MAX:16] are 0, [15:0] may be 0 or 1
      int matches = _mm_movemask_epi8(cmpVec);
      // search through the matches bitmask
      while (matches != 0) {
        // Trailing Zero Count - find the first set bit
        int index = std::countr_zero(static_cast<unsigned int>(matches));
        // get the slot of the associated control byte
        const size_t slotOffset = _getSlotOffset(prevGroupCount, groupIndex, index);
        V* value = reinterpret_cast<V*>(_data.get() + slotOffset);
        _insert(std::move(*value), newData);

        // adjust the bitmask by zeroing out the index we just tried
        matches &= (matches - 1); // bit twiddling hack on trailing 0s
      }
    }

    _data = std::move(newData);
  }

  bool _insert(V v, std::unique_ptr<std::byte[]>& data) {
    const size_t hash = v.hash();
    const uint8_t mostSignificantBits = uint8_t(hash >> 57);
    const Control ctrl{mostSignificantBits};
    // This works because _groupCount is a power of 2
    //                  hash %  _groupCount
    size_t groupIndex = hash & (_groupCount - 1);
    const size_t initialGroupIndex = groupIndex;
    while (true) {
      // first, get the 16 bytes to examine (the group)
      void* group = data.get() + (groupIndex * GroupSize);
      __m128i groupVec = _mm_loadu_si128(reinterpret_cast<__m128i*>(group));
      // broadcast the single-byte control sequence to a 16-byte vector
      __m128i ctrlVec = _mm_set1_epi8(uint8_t(0b1000'0000));
      // AND each byte with the ctrlVec to discard all but the interesting high bit
      __m128i maskedVec = _mm_and_si128(groupVec, ctrlVec);
      // check whether each byte equals each other byte.
      // output 0xFF to result vec in place of each matching byte.
      __m128i cmpVec = _mm_cmpeq_epi8(ctrlVec, maskedVec);
      // get the position of matching bytes. likely 0 or 1 bytes match
      // [MAX:16] are 0, [15:0] may be 0 or 1
      int matches = _mm_movemask_epi8(cmpVec);
      // search through the matches bitmask
      if (matches != 0) {
        // Trailing Zero Count - find the first set bit
        int index = std::countr_zero(static_cast<unsigned int>(matches));
        // get the slot of the associated control byte
        const size_t slotOffset = _getSlotOffset(_groupCount, groupIndex, index);
        V* slot = reinterpret_cast<V*>(data.get() + slotOffset);
        *slot = std::move(v);
        Control* ctrlSlot = reinterpret_cast<Control*>(
          reinterpret_cast<std::byte*>(group) + index);
        *ctrlSlot = ctrl;
        return true;
      }

      // This works because _groupCount is a power of 2
      //           (groupIndex + 1) %  _groupCount;
      groupIndex = (groupIndex + 1) & (_groupCount - 1);
      if (groupIndex == initialGroupIndex) {
        return false;
      }
    }
    return false;
  }

  bool _find(V const& v, Control*& ctrlOut, V*& entryOut) const {
    const size_t hash = v.hash();
    const uint8_t mostSignificantBits = uint8_t(hash >> 57);
    const Control ctrl{mostSignificantBits};
    // This works because _groupCount is a power of 2
    //                  hash %  _groupCount
    size_t groupIndex = hash & (_groupCount - 1);
    const size_t initialGroupIndex = groupIndex;
    while (true) {
      // first, get the 16 bytes to examine (the group)
      void* group = _data.get() + (groupIndex * GroupSize);
      __m128i groupVec = _mm_loadu_si128(reinterpret_cast<__m128i*>(group));
      // broadcast the single-byte control sequence to a 16-byte vector
      __m128i ctrlVec = _mm_set1_epi8(uint8_t(ctrl));
      // check whether each byte equals each other byte.
      // output 0xFF to result vec in place of each matching byte.
      __m128i cmpVec = _mm_cmpeq_epi8(groupVec, ctrlVec);
      // get the position of matching bytes. likely 0 or 1 bytes match
      // [MAX:16] are 0, [15:0] may be 0 or 1
      int matches = _mm_movemask_epi8(cmpVec);
      // search through the matches bitmask
      while (matches != 0) {
        // Trailing Zero Count - find the first set bit
        int index = std::countr_zero(static_cast<unsigned int>(matches));
        // try index's associated value for equality
        const size_t slotOffset = _getSlotOffset(_groupCount, groupIndex, index);
        V* candidate = reinterpret_cast<V*>(_data.get() + slotOffset);
        // this comparison is very likely to succeed
        if (*candidate == v) {
          ctrlOut = reinterpret_cast<Control*>(
              reinterpret_cast<std::byte*>(group) + index);
          entryOut = candidate;
          return true;
        }
        // adjust the bitmask by zeroing out the index we just tried
        matches &= (matches - 1); // bit twiddling hack on trailing 0s
      }
      // We didn't find it in this group. Check whether there are any Empty
      // entries in this group. If there are any, then we can stop looking now
      // since the entry we're looking for would have been here. If we just
      // checked against the highest bit for emptiness (i.e., Empty or Removed),
      // we might see "Removed" and think of it as empty. However, just because
      // there isn't data there doesn't mean that what we're searching for would
      // have been there. Rather, it could have been inserted later in the table
      // while that Removed slot was full. Then, the slot could become Removed
      // before we process this Find operation. So, we need to see a bonafide
      // Empty to stop. Luckily, this is pretty likely.
      ctrlVec = _mm_set1_epi8(uint8_t(Control::Empty));
      cmpVec = _mm_cmpeq_epi8(groupVec, ctrlVec);
      matches = _mm_movemask_epi8(cmpVec);
      if (matches != 0) { // likely!
        return false;
      }

      // This works because _groupCount is a power of 2
      //           (groupIndex + 1) %  _groupCount;
      groupIndex = (groupIndex + 1) & (_groupCount - 1);
      if (groupIndex == initialGroupIndex) {
        return false;
      }
    }
    return false;
  }

  // Get the byte offset in the data array.
  //   groupCount:    how many groups are in the data
  //   groupIndex:    which group are we interested in
  //   entryIndex:    which entry in the group are we interested in
  size_t _getSlotOffset(size_t groupCount, size_t groupIndex, size_t entryIndex) const {
    // Form with 1 fewer multiplication, addition
    return GroupSize * (groupCount + groupIndex * sizeof(V)) + sizeof(V) * entryIndex;
    //return
    //  groupCount * GroupSize +             // metadata
    //  groupIndex * GroupSize * sizeof(V) + // relevant group in slots
    //  sizeof(V) * entryIndex;              // relevant entry in slot group
  }
};

