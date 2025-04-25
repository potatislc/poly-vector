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
  void hi() override { std::cout << "Grolsch" << ++member << '\n'; }
  int member{};
  ~C() override { std::cout << "destroyed C" << ++member << '\n'; }
};

int main() {
  auto vec = somm::poly_vector<A>();
  auto vec2(vec);
  vec = std::move(vec2);
  std::cout << vec.empty() << '\n';
  vec.insert(B());
  vec.insert(C(), sizeof(C), alignof(C));
  vec.at(0)->hi();
  vec.at(1)->hi();
  vec.at(1)->hi();
  vec.at(1)->hi();
  vec.at(1)->hi();
  vec.free(0);
  std::cout << "Address of B: " << vec[0] << '\n';
  std::cout << "Address of C: " << vec[1] << '\n';
  vec.free(1);
  vec.insert(B());
  vec.insert(C());
  vec.at(0)->hi();
  vec.at(1)->hi();
  std::cout << "Vector Size: " << vec.size() << '\n';
  std::cout << vec.empty() << '\n';
  std::cout << sizeof(C) << " " << alignof(C) << '\n';
}