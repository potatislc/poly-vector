#ifndef POLY_VECTOR_H
#define POLY_VECTOR_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace somm {

template <typename Base, size_t MaxBufferByteSize = SIZE_MAX> class PolyVector {
public:
  using data_t = uintptr_t;
  using offset_t = typename std::conditional_t<
      (MaxBufferByteSize / sizeof(data_t) <= UINT8_MAX), uint8_t,
      typename std::conditional_t<
          (MaxBufferByteSize / sizeof(data_t) <= UINT16_MAX), uint16_t,
          typename std::conditional_t<(MaxBufferByteSize / sizeof(data_t) <=
                                       UINT32_MAX),
                                      uint32_t, uint64_t>>>;
  using free_index_t = offset_t;

  struct Iterator {
    Iterator(PolyVector &vector, size_t index)
        : poly_vec(vector), m_index(index) {}

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

  data_t *data() noexcept { return m_buffer.data(); }

  void clear() noexcept {
    m_buffer.clear();
    m_offsets.resize(
        1); // We always need the first element for insertion to work
    m_free_indices.clear();
  }

  Base *operator[](size_t index) noexcept {
    auto &buffer_data = m_buffer[m_offsets[index]];
    if (reinterpret_cast<data_t &>(buffer_data) == free_space)
      return nullptr;

    return reinterpret_cast<Base *>(&buffer_data);
  }

  Base *at(size_t index) {
    check_bounds("at()", index);
    return (*this)[index];
  }

  inline bool is_free(size_t index) const {
    check_bounds("is_free()", index);
    return (*this)[index] == nullptr;
  }

  size_t size_at(size_t index) const {
    check_bounds("size_at()", index);
    return (m_offsets[index + 1] - m_offsets[index]) << 3;
  }

  size_t max_size() const noexcept { return sizeof(offset_t); }

  void free(size_t index) {
    check_bounds("free()", index);
    m_free_indices.emplace_back(index);
    auto *object = reinterpret_cast<Base *>(&m_buffer[m_offsets[index]]);
    object->~Base();
    *reinterpret_cast<data_t *>(object) =
        free_space; // Zeroed the vtable-pointer
  }

  void free_all() {
    for (auto &object : *this) {
      object.~Base();
      reinterpret_cast<data_t &>(object) =
          free_space; // Zeroed the vtable-pointer
    }

    m_offsets.resize(
        1); // We always need the first element for insertion to work
    m_free_indices.clear();
  }

  void shrink_to_fit() noexcept {
    m_buffer.shrink_to_fit();
    m_offsets.shrink_to_fit();
    m_free_indices.shrink_to_fit();
  }

  template <typename Derived> size_t push_back(const Derived &object) noexcept {
    // The last object's end is my start
    offset_t &start = m_offsets.back();
    // Can give the tail of the pervious element some extra buffer space. But it
    // does not matter since it is cast to a smaller Base type when returned
    start = align(start, alignof(Derived));
    offset_t end = start + sizeof(Derived);
    if (start > end)
      return this->size();

    m_buffer.resize(end);
    new (&m_buffer[start])
        Derived(object); // Does not work if copy constructor is deleted
    m_offsets.emplace_back(end);

    return this->size();
  }

  template <typename Derived> size_t push(const Derived &object) noexcept {
    for (auto &index : m_free_indices) {
      offset_t start = m_offsets[index];
      if (start != align(start, alignof(Derived))) {
        continue;
      }

      // Never out of bounds because m_offsets.back() is an extra element
      // without an end, representing a space for the next insert_at_end()
      offset_t end = m_offsets[index + 1];
      if (end - start < sizeof(Derived))
        continue;

      index = m_free_indices.back();
      m_free_indices.pop_back();
      new (&m_buffer[start])
          Derived(object); // Does not work if copy constructor is deleted

      return index;
    }

    return push_back(object);
  }

  template <typename Derived, typename... Args>
  size_t emplace_back(Args &&...args) noexcept {
    // The last object's end is my start
    offset_t &start = m_offsets.back();
    // Can give the tail of the pervious element some extra buffer space. But it
    // does not matter since it is cast to a smaller Base type when returned
    start = align(start, alignof(Derived) >> 3);
    offset_t end = start + (sizeof(Derived) >> 3);
    if (start > end)
      return this->size();

    m_buffer.resize(end);
    new (&m_buffer[start]) Derived(std::forward<Args>(args)...);
    m_offsets.emplace_back(end);

    return size();
  }

  template <typename Derived, typename... Args>
  size_t emplace(Args &&...args) noexcept {
    for (auto &index : m_free_indices) {
      offset_t start = m_offsets[index];
      if (start != align(start, alignof(Derived) >> 3)) {
        continue;
      }

      // Never out of bounds because m_offsets.back() is an extra element
      // without an end, representing a space for the next insert_at_end()
      offset_t end = m_offsets[index + 1];
      if (end - start < sizeof(Derived) >> 3)
        continue;

      index = m_free_indices.back();
      m_free_indices.pop_back();
      new (&m_buffer[start]) Derived(std::forward<Args>(args)...);

      return index;
    }

    return emplace_back<Derived, Args...>(std::forward<Args>(args)...);
  }

  // Memplace: Memcopies object data into buffer without calling constructor

  size_t memplace_back(const Base &object, size_t size,
                       size_t alignment) noexcept {
    // The last object's end is my start
    offset_t &start = m_offsets.back();
    // Can give the tail of the pervious element some extra buffer space. But
    // it does not matter since it is cast to a smaller Base type when
    // returned
    start = align(start, static_cast<offset_t>(alignment >> 3));
    offset_t end = start + static_cast<offset_t>(size >> 3);
    if (start > end)
      return this->size();

    m_buffer.resize(end);
    std::memcpy(&m_buffer[start], static_cast<const void *>(&object), size);
    m_offsets.emplace_back(end);

    return this->size();
  }

  size_t memplace(const Base &object, size_t size, size_t alignment) noexcept {
    for (auto &index : m_free_indices) {
      offset_t start = m_offsets[index];
      if (start != align(start, static_cast<offset_t>(alignment >> 3))) {
        continue;
      }

      // Never out of bounds because m_offsets.back() is an extra element
      // without an end, representing a space for the next insert_at_end()
      offset_t end = m_offsets[index + 1];
      if (end - start < (size >> 3))
        continue;

      index = m_free_indices.back();
      m_free_indices.pop_back();
      std::memcpy(&m_buffer[start], static_cast<const void *>(&object), size);

      return index;
    }

    return memplace_back(object, size, alignment);
  }

private:
  static constexpr data_t free_space = 0;

  inline void check_bounds(const char *caller, size_t index) {
    if (index >= size()) {
      throw std::out_of_range("somm::poly_vector::" + std::string(caller) +
                              ": index " + std::to_string(index) +
                              " not less than size " + std::to_string(size()));
    }
  }

  static inline offset_t align(offset_t offset, offset_t alignment) noexcept {
    return (offset + alignment - 1) & ~(alignment - 1);
  }

  // free_indices[i] : index -> offsets[index] : offset -> buffer[offset] :
  // data
  std::vector<data_t> m_buffer = {0};
  std::vector<offset_t> m_offsets = {0};
  std::vector<free_index_t> m_free_indices;
};

} // namespace somm

#endif