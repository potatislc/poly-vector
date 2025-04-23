#include "src/poly_vector.h"
#include <iostream>
class A {
public:
  virtual void hi() { std::cout << "Hi" << std::endl; }
};

class B : public A {
  void hi() override { std::cout << "Nah" << std::endl; }
};

class C : public A {
  void hi() override { std::cout << "Grolsch" << std::endl; }
};

int main() {
  auto vec = somm::poly_vector<A>();
  vec.insert(B());
  vec.insert(C());
  vec.get(0)->hi();
  vec.get(1)->hi();
  vec.free(0);
  std::cout << vec.get(0) << std::endl;
  vec.insert(C());
  vec.get(0)->hi();
}