#ifndef POLY_VECTOR_H
#define POLY_VECTOR_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace somm {

template <typename Base> class poly_vector {
public:
  using index_t = uint32_t;
  using offset_t = size_t;

  // The last element is only for marking where next insertion is supposed to
  // happen
  offset_t size() const { return m_offsets.size() - 1; }

  Base *get(index_t index) {
    if (index >= m_offsets.size()) {
      return nullptr;
    }

    // Cast pointer to data at index to Base*
    auto *object = reinterpret_cast<Base *>(&m_buffer.at(m_offsets.at(index)));
    // Check if the vtable pointer is zeroed
    if (*reinterpret_cast<uintptr_t *>(object) == 0)
      return nullptr;

    return object;
  }

  void free(index_t index) {
    if (index >= m_offsets.size()) {
      return;
    }

    m_free_indices.emplace_back(index);
    auto *object = reinterpret_cast<Base *>(&m_buffer.at(m_offsets.at(index)));
    object->~Base();
    // Invalidate address
    *reinterpret_cast<uintptr_t *>(&m_buffer.at(m_offsets.at(index))) =
        0; // Zeroed the vtable-pointer
  }

  inline offset_t align(offset_t offset, offset_t alignment) const {
    return (offset + alignment - 1) & ~(alignment - 1);
  }

  index_t insert(const Base &object, offset_t size, offset_t alignment) {
    if (m_free_indices.empty())
      return insert_at_end(object, size, alignment);

    // Try iserting from an index in free list
    for (size_t i = 0; i < m_free_indices.size(); i++) {
      index_t index = m_free_indices.at(i);
      offset_t offset_start = m_offsets.at(index);
      if (offset_start != align(offset_start, alignment)) {
        continue;
      }

      // Never out of bounds because the last index is always the byte after the
      // end of the last object
      offset_t offset_end = m_offsets.at(index + 1);

      if (offset_end - offset_start < size)
        continue;

      m_free_indices[i] = m_free_indices.back();
      m_free_indices.pop_back();

      std::memcpy(&m_buffer.at(offset_start),
                  static_cast<const void *>(&object), size);

      return index;
    }

    return insert_at_end(object, size, alignment);
  }

  template <typename Derived> index_t insert(const Derived &object) {
    offset_t size = sizeof(Derived);
    offset_t alignment = alignof(Derived);
    return insert(object, size, alignment);
  }

  index_t insert_at_end(const Base &object, offset_t size, offset_t alignment) {
    // The last object's end is my start
    offset_t &offset_start = m_offsets.back();
    // Epic rounding to next aligned spot
    // Doesn't matter if the previous object gets some extra unused bytes at the
    // end
    offset_start = align(offset_start, alignment);
    offset_t offset_end = offset_start + size;

    // Resize byte buffer for new object
    m_buffer.resize(offset_end);
    // Copy object into aligned spot
    std::memcpy(&m_buffer.at(offset_start), static_cast<const void *>(&object),
                size);
    // Store the end offset
    m_offsets.push_back(offset_end);

    return static_cast<index_t>(m_offsets.size()) - 1;
  }

private:
  // free_indices[i] : index -> offsets[index] : offset -> buffer[offset] : data
  std::vector<unsigned char> m_buffer;
  std::vector<offset_t> m_offsets = {0};
  std::vector<index_t> m_free_indices;
};

} // namespace somm

#endif