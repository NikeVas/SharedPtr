#pragma once

#include <concepts>
#include <functional>
#include <utility>

namespace detail {


class IControlBlock {
 public:

  using CounterType = size_t; 
  using DestroyerType = void(*)(IControlBlock*);

  IControlBlock(DestroyerType destroyer) : destroyer_(destroyer) {}

  virtual void Delete() = 0;

  virtual void* GetData() = 0;

  DestroyerType GetDestroyer() { return std::exchange(destroyer_, nullptr); }

  void IncShared() { ++shared_count_; }

  void IncWeak() { ++weak_count_; }

  void DecShared() { --shared_count_; }

  void DecWeak() { --weak_count_; }

  auto SharedCount() { return shared_count_; }

  auto WeakCount() { return weak_count_; }

  virtual ~IControlBlock() = default;

 protected:
  DestroyerType destroyer_ = nullptr;
  CounterType shared_count_ = 0;
  CounterType weak_count_ = 0;
};

template <typename T, typename Deleter>
class ControlBlockWithDeleter : public IControlBlock {
 public:
  ControlBlockWithDeleter(T* ptr, Deleter deleter)
      : IControlBlock([](IControlBlock* ptr) {
                        ::operator delete(static_cast<void*>(ptr));
                      }),
        ptr_(ptr), deleter_(std::move(deleter)) {}

  void Delete() override { deleter_(static_cast<T*>(ptr_)); }

  void* GetData() override { return static_cast<void*>(ptr_); }

 private:
  T* ptr_;
  Deleter deleter_; 
};

/* template <typename T, typename Allocator>
class ControlBlockWithAllocator : public IControlBlock {
 public:
  using BaseAllocatorTraits = std::allocator_traits<Allocator>;
  using AllocForCurrent = typename BaseAllocatorTraits::template rebind_alloc<ControlBlockWithAllocator>;
  using AllocatorTraits = typename BaseAllocatorTraits::template rebind_traits<ControlBlockWithAllocator>;

  template <typename... Args>
  ControlBlockWithAllocator(Allocator allocator, Args&&... args)
      : IControlBlock([](IControlBlock* ptr) {
                        auto object = dynamic_cast<ControlBlockWithAllocator*>(ptr);
                        auto alloc = std::move(object->allocator_);
                        AllocatorTraits::destroy(alloc, object);
                        AllocatorTraits::deallocate(alloc, object, 1);
                      }),
        object_(std::forward<Args>(args)...), allocator_(std::move(allocator)) {}

  void Delete() override {
    // TODO: ??? Why object mustn't be destructed?
  }

  void* GetData() override { return static_cast<void*>(&object_); }

 private:
  T object_; 
  AllocForCurrent allocator_; 
}; */

template <typename T, typename Allocator>
class ControlBlockWithAllocator : public IControlBlock {
 public:
  using BaseAllocatorTraits = std::allocator_traits<Allocator>;
  using AllocForCurrent = typename BaseAllocatorTraits::template rebind_alloc<std::byte>;
  using AllocatorTraits = typename BaseAllocatorTraits::template rebind_traits<std::byte>;

  ControlBlockWithAllocator(T* ptr, Allocator allocator)
      : IControlBlock([](IControlBlock* ptr) {
          auto object = dynamic_cast<ControlBlockWithAllocator*>(ptr);
          auto alloc = std::move(object->allocator_);
          AllocatorTraits::destroy(alloc, object);
          auto alloc_for_current = AllocForCurrent(std::move(alloc));
          AllocatorTraits::deallocate(
              alloc_for_current, reinterpret_cast<std::byte*>(object),
              sizeof(ControlBlockWithAllocator) + sizeof(T));
        }),
        ptr_(ptr),
        allocator_(std::move(allocator)) {}

  void Delete() override {
    AllocatorTraits::destroy(allocator_, static_cast<T*>(ptr_));
  }

  void* GetData() override { return static_cast<void*>(ptr_); }

 private:
  T* ptr_;
  AllocForCurrent allocator_; 
};

template <typename T, typename Deleter, typename Allocator> 
class ControlBlockWithAllocatorWithDeleter : public IControlBlock {
  using BaseAllocatorTraits = std::allocator_traits<Allocator>;
  using AllocForCurrent = typename BaseAllocatorTraits::template rebind_alloc<ControlBlockWithAllocatorWithDeleter>;
  using AllocatorTraits = typename BaseAllocatorTraits::template rebind_traits<ControlBlockWithAllocatorWithDeleter>;

  public:
   ControlBlockWithAllocatorWithDeleter(T* ptr, Deleter deleter,
                                        Allocator alloc)
       : IControlBlock([](IControlBlock* ptr) {
           auto object = dynamic_cast<ControlBlockWithAllocatorWithDeleter*>(ptr);
           auto alloc = std::move(object->allocator_);
           AllocatorTraits::destroy(alloc, object);
           AllocatorTraits::deallocate(alloc, object, 1);
         }),
         ptr_(ptr),
         deleter_(std::move(deleter)),
         allocator_(std::move(alloc)) {}

   void Delete() override { deleter_(static_cast<T*>(ptr_)); }

   void* GetData() override { return static_cast<void*>(ptr_); }

  private:
   T* ptr_;
   Deleter deleter_; 
   AllocForCurrent allocator_;
};

}  // namespace detail


template <typename T>
class EnableSharedFromThis;

template <typename T>
class SharedPtr {
 public:
  using element_type = T;  // std::remove_extent_t<T>;
  using reference = element_type&;
  using pointer = element_type*;
  using IControlBlock = detail::IControlBlock;

  // Public constructor

  SharedPtr() {}
  SharedPtr(std::nullptr_t) {}

  template <typename Y>
  requires std::convertible_to<Y*, pointer> 
  SharedPtr(Y* ptr)
      : control_block_(::new detail::ControlBlockWithDeleter(ptr, [](T* ptr){ ::delete ptr; })) {
    control_block_->IncShared();
  }

  template <typename Y, std::invocable<T*> Deleter>
  requires std::convertible_to<Y*, pointer>
  SharedPtr(Y* ptr, Deleter deleter)
      : control_block_(::new detail::ControlBlockWithDeleter(ptr, std::move(deleter))) {
    control_block_->IncShared();
  }

  template <typename Y, std::invocable<T*> Deleter, typename Allocator>
  requires std::convertible_to<Y*, pointer>
  SharedPtr(Y* ptr, Deleter deleter, Allocator alloc) {
    using ControlBlock = detail::ControlBlockWithAllocatorWithDeleter<T, Deleter, Allocator>;
    using BaseAllocatorTraits = std::allocator_traits<Allocator>;
    using AllocForCurrent = typename BaseAllocatorTraits::template rebind_alloc<ControlBlock>;
    using AllocatorTraits = typename BaseAllocatorTraits::template rebind_traits<ControlBlock>;

    auto allocator = AllocForCurrent(std::move(alloc));

    auto control_block_object = AllocatorTraits::allocate(allocator, 1);
    AllocatorTraits::construct(allocator, control_block_object, ptr, std::move(deleter), allocator);

    control_block_ = control_block_object;

    control_block_->IncShared();
  }

  // Copy 
  // TODO: Code duplication

  SharedPtr(const SharedPtr& other) : control_block_(other.control_block_) {
    if (control_block_ != nullptr) {
      control_block_->IncShared();
    }
  }

  SharedPtr& operator=(const SharedPtr& other) {
    if (std::addressof(other) == this) return *this;
    SharedPtr tmp(other);
    swap(tmp);
    return *this;
  }

  template <typename Y>
  requires std::convertible_to<Y*, pointer> 
  SharedPtr(const SharedPtr<Y>& other) : control_block_(other.control_block_) {
    if (control_block_ != nullptr) {
      control_block_->IncShared();
    }
  }

  template <typename Y>
  requires std::convertible_to<Y*, pointer> 
  SharedPtr& operator=(const SharedPtr<Y>& other) {
    SharedPtr tmp(other);
    swap(tmp);
    return *this;
  }

  // Move 

  SharedPtr(SharedPtr&& other) noexcept
      : control_block_(std::exchange(other.control_block_, nullptr)) {}

  SharedPtr& operator=(SharedPtr&& other) {
    if (std::addressof(other) == this) return *this;

    SharedPtr tmp(std::move(other));
    swap(tmp);

    return *this;
  }

  template <typename Y>
  requires std::convertible_to<Y*, pointer> 
  SharedPtr(SharedPtr<Y>&& other) noexcept
    : control_block_(std::exchange(other.control_block_, nullptr)) {}

  template <typename Y>
  requires std::convertible_to<Y*, pointer> 
  SharedPtr& operator=(SharedPtr<Y>&& other) {
    SharedPtr tmp(std::move(other));
    swap(tmp);

    return *this;
  }

  // Destructor

  ~SharedPtr() {
    if (control_block_ == nullptr) return;
    control_block_->DecShared();
    if (control_block_->SharedCount() == 0) {
      control_block_->Delete();
      if (control_block_->WeakCount() == 0) {
        auto destroyer_ = control_block_->GetDestroyer();
        destroyer_(control_block_);
      }
    }
  }

  // operator

  reference operator*() const {
    return *GetData();
  }

  pointer operator->() const {
    return GetData();
  }

  // Modifiers

  void swap(SharedPtr& other) {
    std::swap(control_block_, other.control_block_);
  }

  void reset() {
    SharedPtr tmp;
    swap(tmp);
  }

  template <typename Y>
  requires std::convertible_to<Y*, pointer>
  void reset(Y* ptr) {
    SharedPtr tmp(ptr);
    swap(tmp);
  }

  // Observers

  size_t use_count() const { return control_block_->SharedCount(); }

  pointer get() const {
    return control_block_ == nullptr
               ? nullptr
               : GetData();
  }

 private:
  
  auto GetData() const {
    return static_cast<pointer>(control_block_->GetData());
  }

  SharedPtr(IControlBlock* other) : control_block_(other) {
    control_block_->IncShared();
  }

  IControlBlock* control_block_ = nullptr;

  template <typename Y>
  friend class SharedPtr;

  template <typename Y>
  friend class WeakPtr;

  template <typename Y, typename... Args>
  friend SharedPtr<Y> MakeShared(Args&&...);

  template <typename Y, typename... Args>
  requires std::is_base_of_v<EnableSharedFromThis<Y>, Y>
  friend SharedPtr<Y> MakeShared(Args&&...);

  template <typename Y, typename Alloc, typename... Args> 
  friend SharedPtr<Y> AllocateShared(Alloc, Args&&...);

  template <typename Y>
  friend class EnableSharedFromThis;
  
};

template <typename Y>
SharedPtr(Y*) -> SharedPtr<Y>;

template <typename Y, typename Deleter>
SharedPtr(Y* ptr, Deleter deleter) -> SharedPtr<Y>;

template <typename T>
class WeakPtr {
 public:
  using element_type = T;  // std::remove_extent_t<T>;
  using reference = element_type&;
  using pointer = element_type*;
  using CorrespondingSharedPtr = SharedPtr<T>;
  using IControlBlock = detail::IControlBlock;

  WeakPtr() {}
  
  ~WeakPtr() {
    if (control_block_ == nullptr) return;
    control_block_->DecWeak();
    if (control_block_->WeakCount() == 0 &&
        control_block_->SharedCount() == 0) {
      auto destroyer_ = control_block_->GetDestroyer();
      destroyer_(control_block_);
    }
  }

  // TODO: Code duplication
  // Copy

  WeakPtr(const WeakPtr& other) : control_block_(other.control_block_) {
    if (control_block_ != nullptr) {
      control_block_->IncWeak();
    }
  }

  WeakPtr& operator=(const WeakPtr& other) {
    if (std::addressof(other) == this) return *this;

    WeakPtr tmp(other);
    swap(other);
    return *this;
  }

  template <typename Y>
  requires std::convertible_to<Y*, pointer> 
  WeakPtr(const WeakPtr<Y>& other) : control_block_(other.control_block_) {
    if (control_block_ != nullptr) {
      control_block_->IncWeak();
    }
  }

  template <typename Y>
  requires std::convertible_to<Y*, pointer> 
  WeakPtr& operator=(const WeakPtr<Y>& other) { 
    WeakPtr tmp(other);
    swap(tmp);
    return *this;
  }

  // Move

  WeakPtr(WeakPtr&& other) noexcept
      : control_block_(std::exchange(other.control_block_, nullptr)) {}

  WeakPtr& operator=(WeakPtr&& other) {
    if (std::addressof(other) == this) return *this;

    WeakPtr tmp(std::move(other));
    swap(other);
    return *this;
  }

  template <typename Y>
  requires std::convertible_to<Y*, pointer> 
  WeakPtr(WeakPtr<Y>&& other) noexcept
      : control_block_(std::exchange(other.control_block_, nullptr)) {}

  template <typename Y>
  requires std::convertible_to<Y*, pointer> 
  WeakPtr& operator=(WeakPtr<Y>&& other) {
    WeakPtr tmp(std::move(other));
    swap(other);
    return *this;
  }

  // Convert from SharedPtr

  template <typename Y>
  requires std::convertible_to<Y*, pointer> 
  WeakPtr(const SharedPtr<Y>& other) : control_block_(other.control_block_)  {
    if (control_block_ != nullptr) {
      control_block_->IncWeak();
    }
  }

  template <typename Y>
  requires std::convertible_to<Y*, pointer> 
  WeakPtr& operator=(const SharedPtr<Y>& other) {
    WeakPtr tmp(other);
    swap(tmp);
    return *this;
  }

  // Modifiers

  void swap(WeakPtr& other) { std::swap(control_block_, other.control_block_); }

  // Observers

  size_t use_count() const { return control_block_->SharedCount(); }

  bool expired() const { return use_count() == 0; }

  auto lock() const {
    return CorrespondingSharedPtr(expired() ? nullptr : control_block_);
  }

 private:
  IControlBlock* control_block_ = nullptr;

  template <typename Y>
  friend class WeakPtr;
};

template <typename Y>
WeakPtr(const SharedPtr<Y>&) -> WeakPtr<Y>;


template <typename T, typename... Args>
SharedPtr<T> MakeShared(Args&&... args) {
  auto deleter = [](T* ptr){ ptr->~T(); };
  using ControlBlock = detail::ControlBlockWithDeleter<T, decltype(deleter)>;

  auto buffer = static_cast<std::byte*>(::operator new(sizeof(T) + sizeof(ControlBlock)));
  auto object = ::new(buffer + sizeof(ControlBlock)) T(std::forward<Args>(args)...);
  auto control_block = ::new(buffer) ControlBlock(object, std::move(deleter));

  return {control_block};
}

// template <typename T, typename Alloc, typename... Args> 
// SharedPtr<T> AllocateShared(Alloc alloc, Args&& ... args) {
//   using ControlBlock = detail::ControlBlockWithAllocator<T, Alloc>;
//   using BaseAllocatorTraits = std::allocator_traits<Alloc>;

//   auto allocator = typename BaseAllocatorTraits::template rebind_alloc<ControlBlock>(std::move(alloc));
//   using AllocatorTraits = typename BaseAllocatorTraits::template rebind_traits<ControlBlock>;

  
//   auto control_block = AllocatorTraits::allocate(allocator, 1);
//   AllocatorTraits::construct(allocator, control_block, allocator, std::forward<Args>(args)...);

//   return {control_block};
// }

template <typename T, typename Alloc, typename... Args> 
SharedPtr<T> AllocateShared(Alloc alloc, Args&& ... args) {
  using ControlBlock = detail::ControlBlockWithAllocator<T, Alloc>;
  using BaseAllocatorTraits = std::allocator_traits<Alloc>;

  auto allocator = typename BaseAllocatorTraits::template rebind_alloc<std::byte>(std::move(alloc));
  using AllocatorTraits = typename BaseAllocatorTraits::template rebind_traits<std::byte>;
  
  auto buffer = AllocatorTraits::allocate(allocator, sizeof(T) + sizeof(ControlBlock));
  // TODO: ? std::launder
  auto object = reinterpret_cast<T*>(buffer + sizeof(ControlBlock));
  auto control_block = reinterpret_cast<ControlBlock*>(buffer);
  AllocatorTraits::construct(allocator, object, std::forward<Args>(args)...);
  AllocatorTraits::construct(allocator, control_block, object, std::move(alloc));

  return {control_block};
}


template <typename T>
class EnableSharedFromThis {
 public: 
  SharedPtr<T> shared_from_this() {
    if (control_block_ == nullptr) {
      throw std::bad_weak_ptr();
    }
    return {control_block_};
  }

 private:
  detail::IControlBlock* control_block_ = nullptr;

  template <typename Y, typename... Args>
  requires std::is_base_of_v<EnableSharedFromThis<Y>, Y>
  friend SharedPtr<Y> MakeShared(Args&&...);
};

template <typename T, typename... Args>
// requires std::derived_from<T, EnableSharedFromThis<T>>  //? WHY?
requires std::is_base_of_v<EnableSharedFromThis<T>, T>
SharedPtr<T> MakeShared(Args&&... args) {
  auto deleter = [](T* ptr){ ptr->~T(); };
  using ControlBlock = detail::ControlBlockWithDeleter<T, decltype(deleter)>;

  auto buffer = static_cast<std::byte*>(::operator new(sizeof(T) + sizeof(ControlBlock)));
  auto object = ::new(buffer + sizeof(ControlBlock)) T(std::forward<Args>(args)...);
  auto control_block = ::new(buffer) ControlBlock(object, std::move(deleter));

  object->control_block_ = control_block;

  return {control_block};
}