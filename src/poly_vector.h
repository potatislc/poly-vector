#ifndef POLY_VECTOR_H
#define POLY_VECTOR_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace somm {

using poly_data_t = uintptr_t; // Minimally the size of the vtable pointer
inline constexpr uint8_t poly_data_byte_scale =
    (sizeof(poly_data_t) == 8) ? 3 : 2;

template <typename Base, typename BufferOffset = size_t> class PolyVector {
public:
  using free_index_t = BufferOffset;

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

  // Untested
  void erase_back() noexcept {
    if (size() == 0)
      return;
    m_buffer.erase(*(m_offsets.back() - 1), m_buffer.end());
    m_offsets.erase(m_buffer.back());
  }

  void shrink_to_fit() noexcept {
    m_buffer.shrink_to_fit();
    m_offsets.shrink_to_fit();
    m_free_indices.shrink_to_fit();
  }

  template <typename Derived> size_t push_back(const Derived &object) noexcept {
    return buffer_write_back(
        [&](BufferOffset start) {
          new (&m_buffer[start]) Derived(
              object); /* Does not work if copy constructor is deleted */
        },
        sizeof(Derived), alignof(Derived));
  }

  template <typename Derived> size_t push(const Derived &object) noexcept {
    return buffer_write(
        [&](BufferOffset start) {
          new (&m_buffer[start]) Derived(
              object); /* Does not work if copy constructor is deleted */
        },
        sizeof(Derived), alignof(Derived));
  }

  template <typename Derived, typename... Args>
  size_t emplace_back(Args &&...args) noexcept {
    return buffer_write_back(
        [&](BufferOffset start) {
          new (&m_buffer[start]) Derived(std::forward<Args>(args)...);
        },
        sizeof(Derived), alignof(Derived));
  }

  template <typename Derived, typename... Args>
  size_t emplace(Args &&...args) noexcept {
    return buffer_write(
        [&](BufferOffset start) {
          new (&m_buffer[start]) Derived(std::forward<Args>(args)...);
        },
        sizeof(Derived), alignof(Derived));
  }

  // Memplace: Memcopies object data into buffer without calling constructor

  size_t memplace_back(const Base &object, size_t size,
                       size_t alignment) noexcept {
    return buffer_write_back(
        [&](BufferOffset start) {
          std::memcpy(&m_buffer[start], static_cast<const void *>(&object),
                      size);
        },
        size, alignment);
  }

  size_t memplace(const Base &object, size_t size, size_t alignment) noexcept {
    return buffer_write(
        [&](BufferOffset start) {
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
    BufferOffset &start = m_offsets.back();
    // Can give the tail of the pervious element some extra buffer space. But
    // it does not matter since it is cast to a smaller Base type when
    // returned
    start = align(start, alignment >> poly_data_byte_scale);
    BufferOffset end = start + (size >> poly_data_byte_scale);
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
      BufferOffset start = m_offsets[index];
      if (start != align(start, static_cast<BufferOffset>(
                                    alignment >> poly_data_byte_scale))) {
        continue;
      }

      // Never out of bounds because m_offsets.back() is an extra element
      // without an end, representing a space for the next insert_at_end()
      BufferOffset end = m_offsets[index + 1];
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
      throw std::out_of_range("somm::poly_vector::" + std::string(caller) +
                              ": index " + std::to_string(index) +
                              " not less than size " + std::to_string(size()));
    }
  }

  static inline BufferOffset align(BufferOffset offset,
                                   BufferOffset alignment) noexcept {
    return (offset + alignment - 1) & ~(alignment - 1);
  }

  // free_indices[i] : index -> offsets[index] : offset -> buffer[offset] :
  // data
  std::vector<poly_data_t> m_buffer = {0};
  std::vector<BufferOffset> m_offsets = {0};
  std::vector<free_index_t> m_free_indices;
};

template <typename Base> struct PolyPtr {
public:
  PolyPtr(PolyVector<Base> *vector, size_t index) {
    m_data = vector->at(index);
    m_byte_offset = vector->offset_at(index);
  }

  Base *get() {}

private:
  Base *m_data;
  size_t m_byte_offset;
};

} // namespace somm

#endif