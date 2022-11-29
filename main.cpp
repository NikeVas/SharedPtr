#include <concepts>
#include <iostream>
#include <memory>

#include "smart_pointers.hpp"

template <class T>
struct MyAllocator {
  typedef T value_type;
  MyAllocator() noexcept {}

  template <class U>
  MyAllocator(const MyAllocator<U>&) noexcept {}

  template <class U>
  bool operator==(const MyAllocator<U>&) const noexcept {
    return true;
  }

  template <class U>
  bool operator!=(const MyAllocator<U>&) const noexcept {
    return false;
  }

  T* allocate(const size_t n) const {
    std::cout << "Mallocator:Allocate " << n * sizeof(T) << " bytes\n";
    return static_cast<T*>(std::malloc(n * sizeof(T)));
  }

  void deallocate(T* const p, size_t) const noexcept { std::free(p); }
};

void* operator new(size_t n) {
  std::cout << "Allocate " << n << " bytes\n";
  return std::malloc(n);
}

void operator delete(void* ptr) {
  // std::cout << "Deallocate " << n << " bytes\n";
  return std::free(ptr);
}

void operator delete(void* ptr, size_t n) {
  std::cout << "Deallocate " << n << " bytes\n";
  return std::free(ptr);
}

int main() {
  std::cout << "std::shared_ptr: " << sizeof(std::shared_ptr<int>) << "\n";
  std::cout << "SharedPtr: " << sizeof(SharedPtr<int>) << "\n";
  std::cout << '\n';
  {
    std::cout << "std::shared_ptr | ptr + deleter + alloc\n";
    size_t x, y, z, h;  // 48
    int* p = new int;
    std::shared_ptr<int> s(
        p, [](int* ptr) { ::delete ptr; }, MyAllocator<int>{});
    std::cout << '\n';
  }

  {
    std::cout << "SharedPtr | ptr + deleter + alloc\n";
    size_t x, y, z, h;  // 48
    int* p = new int;
    SharedPtr<int> s(
        p, [](int* ptr) { ::delete ptr; }, MyAllocator<int>{});
    std::cout << '\n';
  }

  {
    std::cout << "std::shared_ptr | ptr + deleter\n";
    size_t x, y, z, h;  // 48
    int* p = new int;
    std::shared_ptr<int> s(p, [](int* ptr) { ::delete ptr; });
    std::cout << '\n';
  }

  {
    std::cout << "SharedPtr | ptr + deleter\n";
    size_t x, y, z, h;  // 48
    int* p = new int;
    SharedPtr<int> s(p, [](int* ptr) { ::delete ptr; });
    std::cout << '\n';
  }

  {
    std::cout << "std::shared_ptr | ptr\n";
    size_t x, y, z, h;  // 48
    int* p = new int;
    std::shared_ptr<int> s(p);
    std::cout << '\n';
  }

  {
    std::cout << "SharedPtr | ptr\n";
    size_t x, y, z, h;  // 48
    int* p = new int;
    SharedPtr<int> s(p);
    std::cout << '\n';
  }

  // std::cout << sizeof(detail::ControlBlockWithDeleter<int, void(*)(int*)>);
  std::cout << sizeof(detail::IControlBlock);
}