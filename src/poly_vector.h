#ifndef POLY_VECTOR_H
#define POLY_VECTOR_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace somm {

template <typename Base, typename BufferOffset = size_t,
          typename FreeIndex = size_t>
class poly_vector {
public:
  using index_t = size_t;

  // Ignore m_offsets.back() since it does not contain any data until next
  // insert_at_end()
  size_t size() const noexcept { return m_offsets.size() - 1; }

  Base *operator[](index_t index) noexcept {
    auto *object = reinterpret_cast<Base *>(&m_buffer[m_offsets[index]]);
    // Check if the vtable pointer is zeroed
    if (*reinterpret_cast<uintptr_t *>(object) == free_space)
      return nullptr;

    return object;
  }

  Base *at(index_t index) {
    check_bounds("at()", index);
    return (*this)[index];
  }

  void free(index_t index) {
    check_bounds("free()", index);

    m_free_indices.emplace_back(index);
    auto *object = reinterpret_cast<Base *>(&m_buffer.at(m_offsets[index]));
    object->~Base();
    *reinterpret_cast<uintptr_t *>(object) =
        free_space; // Zeroed the vtable-pointer
  }

  index_t insert(const Base &object, BufferOffset size,
                 BufferOffset alignment) {
    if (m_free_indices.empty())
      return insert_at_end(object, size, alignment);

    // Try inserting from indices in free list
    for (size_t i = 0; i < m_free_indices.size(); i++) {
      index_t index = m_free_indices.at(i);
      BufferOffset start = m_offsets.at(index);
      if (start != align(start, alignment)) {
        continue;
      }

      // Never out of bounds because m_offsets.back() is an extra element
      // without an end, representing a space for the next insert_at_end()
      BufferOffset end = m_offsets.at(index + 1);

      if (end - start < size)
        continue;

      m_free_indices[i] = m_free_indices.back();
      m_free_indices.pop_back();

      std::memcpy(&m_buffer.at(start), static_cast<const void *>(&object),
                  size);

      return index;
    }

    return insert_at_end(object, size, alignment);
  }

  template <typename Derived> index_t insert(const Derived &object) {
    BufferOffset size = sizeof(Derived);
    BufferOffset alignment = alignof(Derived);
    return insert(object, size, alignment);
  }

  index_t insert_at_end(const Base &object, BufferOffset size,
                        BufferOffset alignment) {
    // The last object's end is my start
    BufferOffset &start = m_offsets.back();
    // Can give the tail of the pervious element some extra buffer space. But it
    // does not matter since it is cast to a smaller Base type when returned
    start = align(start, alignment);
    BufferOffset end = start + size;

    m_buffer.resize(end);
    std::memcpy(&m_buffer.at(start), static_cast<const void *>(&object), size);
    m_offsets.push_back(end);

    return static_cast<index_t>(this->size());
  }

private:
  inline void check_bounds(const char *caller, size_t index) {
    if (index >= size()) {
      throw std::out_of_range("somm::poly_vector::" + std::string(caller) +
                              ": index " + std::to_string(index) +
                              " not less than size " + std::to_string(size()));
    }
  }

  inline BufferOffset align(BufferOffset offset, BufferOffset alignment) const {
    return (offset + alignment - 1) & ~(alignment - 1);
  }

  // free_indices[i] : index -> offsets[index] : offset -> buffer[offset] : data
  std::vector<unsigned char> m_buffer;
  static constexpr BufferOffset MAX_BUFFER_SIZE =
      std::numeric_limits<BufferOffset>::max();
  static constexpr size_t free_space = 0;
  std::vector<BufferOffset> m_offsets = {0};
  std::vector<FreeIndex> m_free_indices;
};

} // namespace somm

#endif