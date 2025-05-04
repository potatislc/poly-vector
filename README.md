# Poly Vector

**PolyVector** is a C++ sequence container for storing polymorphic subtypes of an abstract base class in a **packed, cache friendly-format**.

## Motivation

In traditional polymorphic containers like `std::vector<Base\*>`, each element is a pointer to a separately heap-allocated object. With this approach comes:

- Poor spatial locality
- Increased heap fragmentation
- Heap allocation overhead

PolyVector solves this by storing polymorphic subtypes inline in a contiguous byte buffer, regardless of size or alignment. Each object's position in the buffer is tracked via an internal offset table.

This approach keeps all data tightly packed, improving cache performance and reducing allocation overhead, while still supporting safe polymorphic access through the base class.

## Order never changes

Once elements are inserted into a PolyVector, their order is preserved until the container is explicitly cleared with `somm::PolyVector::clear()`. You can therefore safely store and reuse indices into the **PolyVector** without worrying about invalidation.
