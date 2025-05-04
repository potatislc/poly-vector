#ifndef POLY_VECTOR_H
#define POLY_VECTOR_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace somm {

template <typename Base, typename Derived> constexpr void assert_must_derive() {
  static_assert(std::is_base_of<Base, Derived>::value,
                "Type must derive from Base");
}

using poly_data_t = uintptr_t; // Size of the vtable pointer defines the minimum
                               // buffer data size and alignment
inline constexpr uint8_t poly_data_byte_scale =
    (sizeof(poly_data_t) == 8) ? 3 : 2;

template <typename Base> class PolyVector {
public:
  static_assert(std::is_abstract<Base>(),
                "Base class must be an abstract class");
  using buffer_offset_t = size_t;
  using free_index_t = buffer_offset_t;

  struct Iterator {
    using iterator_category = std::forward_iterator_tag;
    using value_type = Base;
    using difference_type = std::ptrdiff_t;
    using pointer = Base *;
    using reference = Base &;

    Iterator(PolyVector *vector, size_t index)
        : poly_vec(vector), m_index(index) {
      skip_nulls();
    }

    pointer operator->() const { return &**this; }

    reference operator*() const {
      if (m_index >= poly_vec->size()) {
        throw std::out_of_range(
            "somm::PolyVector::Iterator dereference: Resource at index: " +
            std::to_string(m_index) + "is freed");
      }

      return *(*poly_vec)[m_index];
    };

    Iterator &operator++() {
      ++m_index;
      skip_nulls();
      return *this;
    }

    Iterator operator++(int) {
      Iterator temp = *this;
      ++(*this);
      return temp;
    }

    bool operator==(const Iterator &other) const {
      return poly_vec == other.poly_vec && m_index == other.m_index;
    }

    bool operator!=(const Iterator &other) const { return !(*this == other); }

  private:
    void skip_nulls() {
      while (m_index < poly_vec->size() && (*poly_vec)[m_index] == nullptr) {
        ++m_index;
      }
    }

    PolyVector *poly_vec;
    size_t m_index;
  };

  Iterator begin() {
    if (empty())
      return end();
    return {this, 0};
  }

  Iterator end() { return {this, size()}; }

  Iterator back() { return {this, (size()) ? size() - 1 : 0}; }

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
      : m_buffer(std::move(other.m_buffer)),
        m_offsets(std::move(other.m_offsets)),
        m_free_indices(std::move(other.m_free_indices)) {}

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

  size_t max_size() const noexcept {
    return (m_buffer.size() << poly_data_byte_scale) / sizeof(Base);
  }

  poly_data_t *buffer_data() noexcept { return m_buffer.data(); }

  poly_data_t *offset_data() noexcept { return m_offsets.data(); }

  poly_data_t *free_indices_data() noexcept { return m_free_indices.data(); }

  void clear() noexcept {
    m_buffer.clear();
    m_offsets.resize(
        1); // We always need the first element for insertion to work
    m_free_indices.clear();
  }

  Base *operator[](size_t index) noexcept {
    auto &buffer_data = m_buffer[m_offsets[index]];
    if (reinterpret_cast<poly_data_t &>(buffer_data) == free_space)
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
    return (m_offsets[index + 1] - m_offsets[index]) << poly_data_byte_scale;
  }

  size_t offset_at(size_t index) {
    check_bounds("offset_at()", index);
    return m_offsets[index];
  }

  void free(size_t index) {
    check_bounds("free()", index);
    m_free_indices.emplace_back(index);
    auto *object = reinterpret_cast<Base *>(&m_buffer[m_offsets[index]]);
    object->~Base();
    *reinterpret_cast<poly_data_t *>(object) =
        free_space; // Zeroed the vtable-pointer
  }

  void free_all() {
    for (auto &object : *this) {
      object.~Base();
      reinterpret_cast<poly_data_t &>(object) =
          free_space; // Zeroed the vtable-pointer
    }

    m_offsets.resize(
        1); // We always need the first element for insertion to work
    m_free_indices.clear();
  }

  // An evil function that goes against the philisophy of the class. A
  // PolyVector should never shrink, no element should ever be moved or
  // destroyed. PolyVector should only be able to grow or be cleared.
  // void erase_back() {
  //   if (size() <= 1)
  //     return;
  //   m_buffer.erase(m_buffer.begin() +
  //                      static_cast<std::ptrdiff_t>((m_offsets.back() - 1)),
  //                  m_buffer.end());
  //   m_offsets.pop_back();
  // }

  void shrink_to_fit() noexcept {
    m_buffer.shrink_to_fit();
    m_offsets.shrink_to_fit();
    m_free_indices.shrink_to_fit();
  }

  void reserve_buffer(size_t bytes) {
    size_t datas = bytes << poly_data_byte_scale;
    m_buffer.reserve(datas);
  }

  void reserve_elements(size_t n) {
    m_offsets.reserve(n);
    m_free_indices.reserve(n);
  }

  template <typename Derived> size_t push_back(const Derived &object) noexcept {
    assert_must_derive<Base, Derived>();
    return buffer_write_back(
        [&](buffer_offset_t start) {
          new (&m_buffer[start]) Derived(
              object); /* Does not work if copy constructor is deleted */
        },
        sizeof(Derived), alignof(Derived));
  }

  template <typename Derived> size_t push(const Derived &object) noexcept {
    assert_must_derive<Base, Derived>();
    return buffer_write(
        [&](buffer_offset_t start) {
          new (&m_buffer[start]) Derived(
              object); /* Does not work if copy constructor is deleted */
        },
        sizeof(Derived), alignof(Derived));
  }

  template <typename Derived, typename... Args>
  size_t emplace_back(Args &&...args) noexcept {
    assert_must_derive<Base, Derived>();
    return buffer_write_back(
        [&](buffer_offset_t start) {
          new (&m_buffer[start]) Derived(std::forward<Args>(args)...);
        },
        sizeof(Derived), alignof(Derived));
  }

  template <typename Derived, typename... Args>
  size_t emplace(Args &&...args) noexcept {
    assert_must_derive<Base, Derived>();
    return buffer_write(
        [&](buffer_offset_t start) {
          new (&m_buffer[start]) Derived(std::forward<Args>(args)...);
        },
        sizeof(Derived), alignof(Derived));
  }

  // Memplace: Memcopies object data into buffer without calling constructor

  size_t memplace_back(const Base &object, size_t size,
                       size_t alignment) noexcept {
    return buffer_write_back(
        [&](buffer_offset_t start) {
          std::memcpy(&m_buffer[start], static_cast<const void *>(&object),
                      size);
        },
        size, alignment);
  }

  size_t memplace(const Base &object, size_t size, size_t alignment) noexcept {
    return buffer_write(
        [&](buffer_offset_t start) {
          std::memcpy(&m_buffer[start], static_cast<const void *>(&object),
                      size);
        },
        size, alignment);
  }

private:
  static constexpr poly_data_t free_space = 0;

  template <typename WriterFunction>
  size_t buffer_write_back(WriterFunction &&write, size_t size,
                           size_t alignment) noexcept {
    // The last object's end is my start
    buffer_offset_t &start = m_offsets.back();
    // Can give the tail of the pervious element some extra buffer space. But
    // it does not matter since it is cast to a smaller Base type when
    // returned
    start = align(start, alignment >> poly_data_byte_scale);
    buffer_offset_t end = start + (size >> poly_data_byte_scale);
    if (start > end)
      return this->size();

    m_buffer.resize(end);
    write(start);
    m_offsets.emplace_back(end);

    return this->size();
  }

  template <typename WriterFunction>
  size_t buffer_write(WriterFunction &&write, size_t size,
                      size_t alignment) noexcept {
    for (auto &index : m_free_indices) {
      buffer_offset_t start = m_offsets[index];
      if (start != align(start, static_cast<buffer_offset_t>(
                                    alignment >> poly_data_byte_scale))) {
        continue;
      }

      // Never out of bounds because m_offsets.back() is an extra element
      // without an end, representing a space for the next insert_at_end()
      buffer_offset_t end = m_offsets[index + 1];
      if (end - start < (size >> poly_data_byte_scale))
        continue;

      index = m_free_indices.back();
      m_free_indices.pop_back();
      write(start);

      return index;
    }

    return buffer_write_back(write, size, alignment);
  }

  inline void check_bounds(const char *caller, size_t index) {
    if (index >= size()) {
      throw std::out_of_range("somm::PolyVector::" + std::string(caller) +
                              ": index " + std::to_string(index) +
                              " not less than size " + std::to_string(size()));
    }
  }

  static inline buffer_offset_t align(buffer_offset_t offset,
                                      buffer_offset_t alignment) noexcept {
    return (offset + alignment - 1) & ~(alignment - 1);
  }

  // free_indices[i] : index -> offsets[index] : offset -> buffer[offset] :
  // data
  std::vector<poly_data_t> m_buffer = {0};
  std::vector<buffer_offset_t> m_offsets = {0};
  std::vector<free_index_t> m_free_indices;
};

} // namespace somm

#endif