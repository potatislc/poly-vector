#ifndef POLY_VECTOR_H
#define POLY_VECTOR_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace somm {

template <typename Base, typename BufferOffset = size_t,
          typename FreeIndex = size_t>
class PolyVector {
public:
  struct Iterator {
    Iterator(PolyVector &vector, size_t index)
        : poly_vec(vector), m_index(index) {}

    // What if someone frees the index while it is referenced? Explosion?
    // Should return a reference to an index_ptr instead when it is implemented
    Base &operator*() {
      // Failsafe for when first element is freed OOPS!
      while (m_index < poly_vec.size() && poly_vec[m_index] == nullptr) {
        ++m_index;
      }

      return *poly_vec[m_index];
    };

    Iterator &operator++() {
      do {
        ++m_index;
      } while (m_index < poly_vec.size() && poly_vec[m_index] == nullptr);

      return *this;
    }

    bool operator!=(const Iterator &other) { return m_index != other.m_index; }

  private:
    // Should be a pointer to m_buffer.data(), it will break when poly_vec
    // reallocates, but so does std::vector::iterator
    PolyVector &poly_vec;
    size_t m_index;
  };

  Iterator begin() {
    if (empty())
      return end();
    return {*this, 0};
  }

  Iterator end() { return {*this, size()}; }

  Iterator back() { return {*this, (size()) ? size() - 1 : 0}; }

  PolyVector() noexcept = default;

  ~PolyVector() noexcept {
    for (auto &object : *this) {
      object.~Base();
    }
  }

  PolyVector(const PolyVector &other) noexcept
      : m_buffer(other.m_buffer), m_offsets(other.m_offsets),
        m_free_indices(other.m_free_indices) {}

  PolyVector &operator=(const PolyVector &other) noexcept {
    m_buffer = other.m_buffer;
    m_offsets = other.m_offsets;
    m_free_indices = other.m_free_indices;
    return this;
  }

  PolyVector(const PolyVector &&other) noexcept
      : m_buffer(std::move(other.m_buffer),
                 m_offsets(std::move(other.m_offsets)),
                 m_free_indices(std::move(other.m_free_indices))) {}

  PolyVector &operator=(const PolyVector &&other) noexcept {
    m_buffer = std::move(other.m_buffer);
    m_offsets = std::move(other.m_offsets);
    m_free_indices = std::move(other.m_free_indices);
    return *this;
  }

  // Ignore m_offsets.back() since it does not contain any data until next
  // insert_at_end()
  size_t size() const noexcept { return m_offsets.size() - 1; }

  bool empty() const noexcept { return m_free_indices.size() == size(); }

  unsigned char *data() noexcept { return m_buffer.data(); }

  void clear() noexcept {
    m_buffer.clear();
    m_offsets.resize(
        1); // We always need the first element for insertion to work
    m_free_indices.clear();
  }

  Base *operator[](size_t index) noexcept {
    auto &buffer_data = m_buffer[m_offsets[index]];
    if (reinterpret_cast<uintptr_t &>(buffer_data) == free_space)
      return nullptr;

    return reinterpret_cast<Base *>(&buffer_data);
  }

  Base *at(size_t index) {
    check_bounds("at()", index);
    return (*this)[index];
  }

  inline bool is_free(size_t index) {
    check_bounds("is_free()", index);
    return (*this)[index] == nullptr;
  }

  void free(size_t index) {
    check_bounds("free()", index);

    m_free_indices.emplace_back(index);
    auto *object = reinterpret_cast<Base *>(&m_buffer[m_offsets[index]]);
    object->~Base();
    *reinterpret_cast<uintptr_t *>(object) =
        free_space; // Zeroed the vtable-pointer
  }

  void free_all() {
    for (auto &object : *this) {
      object.~Base();
      reinterpret_cast<uintptr_t &>(object) =
          free_space; // Zeroed the vtable-pointer
    }

    m_offsets.resize(
        1); // We always need the first element for insertion to work
    m_free_indices.clear();
  }

  // TODO: memplace and memplace_back that are not templated functions and do
  // not call a constructor

  template <typename Derived> size_t push_back(const Derived &object) noexcept {
    // The last object's end is my start
    BufferOffset &start = m_offsets.back();
    // Can give the tail of the pervious element some extra buffer space. But it
    // does not matter since it is cast to a smaller Base type when returned
    start = align(start, alignof(Derived));
    BufferOffset end = start + sizeof(Derived);
    if (start > end)
      return this->size();

    m_buffer.resize(end);
    new (&m_buffer[start])
        Derived(object); // Does not work if copy constructor is deleted
    m_offsets.emplace_back(end);

    return this->size();
  }

  template <typename Derived> size_t push(const Derived &object) {
    if (m_free_indices.empty())
      return push_back(object);

    // Try inserting from indices in free list
    size_t free_size = m_free_indices.size();
    for (size_t i = 0; i < free_size; i++) {
      size_t index = m_free_indices[i];
      BufferOffset start = m_offsets[index];
      if (start != align(start, alignof(Derived))) {
        continue;
      }

      // Never out of bounds because m_offsets.back() is an extra element
      // without an end, representing a space for the next insert_at_end()
      BufferOffset end = m_offsets[index + 1];

      if (end - start < sizeof(Derived))
        continue;

      m_free_indices[i] = m_free_indices.back();
      m_free_indices.pop_back();
      new (&m_buffer[start])
          Derived(object); // Does not work if copy constructor is deleted

      return index;
    }

    return push_back(object);
  }

  template <typename Derived, typename... Args>
  size_t emplace_back(Args &&...args) {
    // The last object's end is my start
    BufferOffset &start = m_offsets.back();
    // Can give the tail of the pervious element some extra buffer space. But it
    // does not matter since it is cast to a smaller Base type when returned
    start = align(start, alignof(Derived));
    BufferOffset end = start + sizeof(Derived);
    if (start > end)
      return this->size();

    m_buffer.resize(end);
    new (&m_buffer[start]) Derived(std::forward<Args>(args)...);
    m_offsets.emplace_back(end);

    return size();
  }

  template <typename Derived, typename... Args> size_t emplace(Args &&...args) {
    if (m_free_indices.empty())
      return emplace_back<Derived, Args...>(std::forward<Args>(args)...);

    // Try inserting from indices in free list
    size_t free_size = m_free_indices.size();
    for (size_t i = 0; i < free_size; i++) {
      size_t index = m_free_indices[i];
      BufferOffset start = m_offsets[index];
      if (start != align(start, alignof(Derived))) {
        continue;
      }

      // Never out of bounds because m_offsets.back() is an extra element
      // without an end, representing a space for the next insert_at_end()
      BufferOffset end = m_offsets[index + 1];

      if (end - start < sizeof(Derived))
        continue;

      m_free_indices[i] = m_free_indices.back();
      m_free_indices.pop_back();
      new (&m_buffer[start]) Derived(std::forward<Args>(args)...);

      return index;
    }

    return emplace_back<Derived, Args...>(std::forward<Args>(args)...);
  }

private:
  static constexpr uintptr_t free_space = 0;

  inline void check_bounds(const char *caller, size_t index) {
    if (index >= size()) {
      throw std::out_of_range("somm::poly_vector::" + std::string(caller) +
                              ": index " + std::to_string(index) +
                              " not less than size " + std::to_string(size()));
    }
  }

  inline BufferOffset align(BufferOffset offset,
                            BufferOffset alignment) const noexcept {
    return (offset + alignment - 1) & ~(alignment - 1);
  }

  // free_indices[i] : index -> offsets[index] : offset -> buffer[offset] : data
  std::vector<unsigned char> m_buffer =
      std::vector<unsigned char>(sizeof(uintptr_t), free_space);
  std::vector<BufferOffset> m_offsets = {0};
  std::vector<FreeIndex> m_free_indices;
};

} // namespace somm

#endif