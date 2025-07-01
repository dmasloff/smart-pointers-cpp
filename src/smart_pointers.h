//
// Created by Даниил Маслов on 18.05.2023.
//

#ifndef SHAREDPTR_SMART_POINTERS_H
#define SHAREDPTR_SMART_POINTERS_H

#include <iostream>

template <typename T>
class SharedPtr;

template <typename T>
class WeakPtr;

template <typename T>
class EnableSharedFromThis;

struct base_block {
    base_block(size_t shared, size_t weak)
        : _shared_counter(shared), _weak_counter(weak){};

    size_t _shared_counter = 0;
    size_t _weak_counter = 0;

    virtual void destroy() = 0;
    virtual void deallocate() = 0;
    virtual ~base_block() = default;
};

template <typename T, typename Deleter = std::default_delete<T>,
          typename Allocator = std::allocator<T>>
struct regular_block : public base_block {
    regular_block(size_t shared, size_t weak, T* ptr)
        : base_block(shared, weak), _pointer(ptr){};

    regular_block(size_t shared, size_t weak, T* ptr, Deleter del)
        : base_block(shared, weak), _pointer(ptr), _deleter(del){};

    regular_block(size_t shared, size_t weak, T* ptr, Deleter del,
                  Allocator alloc)
        : base_block(shared, weak),
          _pointer(ptr),
          _deleter(del),
          _allocator(alloc){};

    T* _pointer;
    Deleter _deleter = Deleter();
    Allocator _allocator = Allocator();

    void destroy() override {
        if (_pointer != nullptr) {
            _deleter(_pointer);
            _pointer = nullptr;
        }
    }

    void deallocate() override {
        using block_alloc =
            typename std::allocator_traits<Allocator>::template rebind_alloc<
                regular_block<T, Deleter, Allocator>>;
        block_alloc alloc = _allocator;
        std::allocator_traits<block_alloc>::deallocate(alloc, this, 1);
    }

    ~regular_block() override = default;
};

template <typename T, typename Allocator = std::allocator<T>>
struct shared_block : public base_block {
    T _object;
    Allocator _allocator = Allocator();

    template <typename... Args>
    shared_block(size_t shared, size_t weak, Allocator allocator,
                 Args&&... args)
        : base_block(shared, weak),
          _object(std::forward<Args>(args)...),
          _allocator(allocator) {}

    void destroy() override {
        std::allocator_traits<Allocator>::destroy(_allocator, &_object);
    }

    void deallocate() override {
        using block_alloc = typename std::allocator_traits<
            Allocator>::template rebind_alloc<shared_block<T, Allocator>>;
        block_alloc alloc = _allocator;
        std::allocator_traits<block_alloc>::deallocate(alloc, this, 1);
    }

    ~shared_block() override = default;
};

template <typename T>
class SharedPtr {
    SharedPtr(int val, T* ptr, base_block* control_block)
        : _ptr(ptr), _control_block(control_block) {
        if (_control_block != nullptr) {
            _control_block->_shared_counter += val;
        }
    }

  public:
    using type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using block = base_block;
    using block_pointer = base_block*;

    template <typename U>
    friend class SharedPtr;

    template <typename U>
    friend class WeakPtr;

    template <typename U, typename Allocator, typename... Args>
    friend SharedPtr<U> allocateShared(const Allocator& allocator,
                                       Args&&... args);

    template <typename U>
    friend class EnableSharedFromThis;

    SharedPtr() : _ptr(nullptr), _control_block(nullptr){};

    template <typename U, typename Deleter = std::default_delete<U>,
              typename Allocator = std::allocator<T>>
    SharedPtr(U* ptr, Deleter del = Deleter(),
              Allocator allocator = Allocator())
        : _ptr(ptr) {
        using block_type = regular_block<U, Deleter, Allocator>;
        using block_alloc = typename std::allocator_traits<
            Allocator>::template rebind_alloc<block_type>;
        if constexpr (std::is_base_of_v<EnableSharedFromThis<U>, U>) {
            SharedPtr<U> shared_ptr = ptr->shared_from_this();
            if (shared_ptr.use_count() != 0) {
                *this = shared_ptr;
                return;
            }
            block_alloc alloc = allocator;
            _control_block =
                std::allocator_traits<block_alloc>::allocate(alloc, 1);
            new (_control_block) block_type(1, 0, ptr, del, allocator);
            ptr->set_pointer(*this);
        } else {
            block_alloc alloc = allocator;
            _control_block =
                std::allocator_traits<block_alloc>::allocate(alloc, 1);
            new (_control_block) block_type(1, 0, ptr, del, allocator);
        }
    }

    SharedPtr(const SharedPtr& another)
        : _ptr(another._ptr), _control_block(another._control_block) {
        if (_control_block != nullptr) {
            _control_block->_shared_counter += 1;
        }
    }

    template <typename U>
    SharedPtr(const SharedPtr<U>& another)
        : _ptr(dynamic_cast<T*>(another._ptr)),
          _control_block(another._control_block) {
        if (_control_block != nullptr) {
            ++(_control_block->_shared_counter);
        }
    }

    SharedPtr(SharedPtr&& another) noexcept
        : _ptr(another._ptr), _control_block(another._control_block) {
        another._ptr = nullptr;
        another._control_block = nullptr;
    }

    template <typename U>
    SharedPtr(SharedPtr<U>&& another) noexcept
        : _ptr(dynamic_cast<T*>(another._ptr)),
          _control_block(another._control_block) {
        another._ptr = nullptr;
        another._control_block = nullptr;
    }

    SharedPtr& operator=(const SharedPtr& another) {
        SharedPtr(another).swap(*this);
        return *this;
    }

    template <typename U>
    SharedPtr& operator=(const SharedPtr<U>& another) {
        SharedPtr(another).swap(*this);
        return *this;
    }

    SharedPtr& operator=(SharedPtr&& another) noexcept {
        SharedPtr(std::move(another)).swap(*this);
        return *this;
    }

    template <typename U>
    SharedPtr& operator=(SharedPtr<U>&& another) {
        SharedPtr(std::move(another)).swap(*this);
        return *this;
    }

    pointer get() {
        return _ptr;
    }

    const_pointer get() const {
        return _ptr;
    }

    pointer operator->() {
        return _ptr;
    }

    const_pointer operator->() const {
        return _ptr;
    }

    reference operator*() {
        return *_ptr;
    }

    const_reference operator*() const {
        return *_ptr;
    }

    size_t use_count() const noexcept {
        if (_control_block == nullptr) {
            return 0;
        }
        return _control_block->_shared_counter;
    }

    void reset() {
        auto copy = SharedPtr();
        swap(copy);
    }

    template <typename U, typename Deleter = std::default_delete<U>,
              typename Allocator = std::allocator<U>>
    void reset(U* ptr, Deleter deleter = Deleter(),
               Allocator allocator = Allocator()) {
        auto copy = SharedPtr(ptr, deleter, allocator);
        swap(copy);
    }

    ~SharedPtr() {
        if (_control_block == nullptr) {
            return;
        }
        --(_control_block->_shared_counter);
        if (_control_block->_shared_counter == 0 && _ptr != nullptr) {
            _control_block->destroy();
            _ptr = nullptr;
        }
        if (_control_block->_shared_counter == 0 &&
            _control_block->_weak_counter == 0) {
            _control_block->deallocate();
        }
    }

    void swap(SharedPtr& another) {
        std::swap(_ptr, another._ptr);
        std::swap(_control_block, another._control_block);
    }

    template <typename U>
    void swap(SharedPtr<U>& another) {
        SharedPtr copy(another);
        another = *this;
        swap(copy);
    }

  private:
    pointer _ptr = nullptr;
    block_pointer _control_block = nullptr;
};

template <typename T, typename Allocator = std::allocator<T>, typename... Args>
SharedPtr<T> allocateShared(const Allocator& allocator = Allocator(),
                            Args&&... args) {
    using block_alloc = typename std::allocator_traits<
        Allocator>::template rebind_alloc<shared_block<T, Allocator>>;
    block_alloc alloc = allocator;
    shared_block<T, Allocator>* control_block =
        std::allocator_traits<block_alloc>::allocate(alloc, 1);
    std::allocator_traits<block_alloc>::construct(
        alloc, control_block, 1, 0, allocator, std::forward<Args>(args)...);
    T* ptr = &(control_block->_object);
    return SharedPtr<T>(0, ptr, static_cast<base_block*>(control_block));
};

template <typename T, typename... Args>
SharedPtr<T> makeShared(Args&&... args) {
    return allocateShared<T>(std::allocator<T>(), std::forward<Args>(args)...);
};

template <typename T>
class WeakPtr {
  public:
    using type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using block = base_block;
    using block_pointer = base_block*;

    template <typename U>
    friend class WeakPtr;

    WeakPtr() : _ptr(nullptr), _control_block(nullptr){};

    WeakPtr(const WeakPtr& another)
        : _ptr(another._ptr), _control_block(another._control_block) {
        if (_control_block != nullptr) {
            ++_control_block->_weak_counter;
        }
    }

    template <typename U>
    WeakPtr(const WeakPtr<U>& another)
        : _ptr(another._ptr), _control_block(another._control_block) {
        if (_control_block != nullptr) {
            ++_control_block->_weak_counter;
        }
    }

    WeakPtr(WeakPtr&& another)
        : _ptr(another._ptr), _control_block(another._control_block) {
        another._ptr = nullptr;
        another._control_block = nullptr;
    }

    template <typename U>
    WeakPtr(WeakPtr<U>&& another)
        : _ptr(another._ptr), _control_block(another._control_block) {
        another._ptr = nullptr;
        another._control_block = nullptr;
    }

    WeakPtr& operator=(const WeakPtr& another) {
        WeakPtr copy(another);
        swap(copy);
        return *this;
    }

    template <typename U>
    WeakPtr& operator=(const WeakPtr<U>& another) {
        WeakPtr copy(another);
        swap(copy);
        return *this;
    }

    template <typename U>
    WeakPtr(const SharedPtr<U>& another)
        : _ptr(another._ptr), _control_block(another._control_block) {
        if (_control_block != nullptr) {
            ++_control_block->_weak_counter;
        }
    }

    SharedPtr<T> lock() const noexcept {
        return SharedPtr<T>(1, _ptr, _control_block);
    }

    size_t use_count() const noexcept {
        if (_control_block == nullptr) {
            return 0;
        }
        return _control_block->_shared_counter;
    }

    bool expired() const noexcept {
        return use_count() == 0;
    }

    ~WeakPtr() {
        if (_control_block == nullptr) {
            return;
        }
        --_control_block->_weak_counter;
        if (_control_block->_shared_counter == 0 &&
            _control_block->_weak_counter == 0) {
            _control_block->deallocate();
        }
    }

    void swap(WeakPtr& another) {
        std::swap(_ptr, another._ptr);
        std::swap(_control_block, another._control_block);
    }

    template <typename U>
    void swap(WeakPtr<U>& another) {
        WeakPtr copy(another);
        another = *this;
        swap(copy);
    }

  private:
    pointer _ptr = nullptr;
    block_pointer _control_block = nullptr;
};

template <typename T>
class EnableSharedFromThis {
  public:
    SharedPtr<T> shared_from_this() const noexcept {
        return _weak_ptr.lock();
    }

    template <typename U>
    void set_pointer(const SharedPtr<U>& ptr) {
        _weak_ptr = ptr;
    }

  private:
    WeakPtr<T> _weak_ptr = WeakPtr<T>();
};

#endif  //SHAREDPTR_SMART_POINTERS_H