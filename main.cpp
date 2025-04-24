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
  vec.insert(B());
  vec.insert(C(), sizeof(C), alignof(C));
  vec.get(0)->hi();
  vec.get(1)->hi();
  vec.get(1)->hi();
  vec.get(1)->hi();
  vec.get(1)->hi();
  vec.free(0);
  vec.free(1);
  std::cout << vec.get(0) << std::endl;
  vec.insert(B());
  vec.insert(C());
  vec.get(0)->hi();
  vec.get(1)->hi();
  std::cout << "Vector Size: " << vec.size() << '\n';
}