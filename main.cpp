#include "src/poly_vector.h"
#include <iostream>

struct A {
  virtual void hi() = 0;
  virtual ~A() = default;
};

struct B : A {
  void hi() override { std::cout << "Nah" << '\n'; }
  ~B() override { std::cout << "destroyed B" << '\n'; }
};

struct C : A {
  C(int member) : member(member) {}
  void hi() override { std::cout << "Grolsch" << ++member << '\n'; }
  int member{};
  ~C() override { std::cout << "destroyed C" << ++member << '\n'; }
};

int main() {
  somm::PolyVector<A> vec;
  vec.emplace<B>();
  vec.emplace<C>(36789);
  vec.emplace_back<B>();
  vec.push(B());
  vec.push_back(C(2));

  for (auto &object : vec) {
    object.hi();
  }

  vec.free_all();
}