# tlsf-pmr

tlsf-pmr is a memory resource for use with [`polymorphic_allocator`](https://en.cppreference.com/w/cpp/memory/polymorphic_allocator) that uses the Two-Level Segregated Fit algorithm as an allocation scheme. This algorithm is able to allocate memory in $O(1)$ time, and is thus suitable in contexts where dynamic memory allocation with deterministic latency is needed, e.g. real-time operating systems or audio applications.

The original implementation was implemented in C with a GPL license. This project is based off of a clean-room implementation by [mattconte](https://github.com/mattconte/tlsf) and adapted to use C++ best practices and enable a cleaner API. It has no external dependencies aside from the standard library, so incorporating it into your project should be very straightforward.

## Requirements
- C++17 compiler and standard library. In practice your choice is either libstdc++ 9.0+ or MSVC STL 19.13+ as libc++ still does not support `polymorphic_memory_resource`. See [this page](https://en.cppreference.com/w/cpp/compiler_support) for details.
- 32-bit or 64-bit architecture.

## Usage
tlsf-pmr can be used with any container that accepts the standard library memory allocator API when combined with `std::polymorphic_allocator`. This includes standard library containers such as `std::vector`. Note that there is memory overhead from the memory block headers. Each block header requires 32 bytes of memory. 

```cpp
using namespace tlsf;
{
    tlsf_resource resource(5000); //pool with 5000 bytes of memory
    std::pmr::polymorphic_allocator<TVal> alloc(&resource);

    //std::pmr::vector<T> is a type alias for std::vector<T, std::pmr::polymorphic_allocator<T>>
    std::pmr::vector<int> vec(alloc); //vector now uses the allocator

    vec.push_back(50);
} //all memory is automatically deallocated when tlsf_resource exits scope.
```

## Disclaimer on performance
Note that deterministic latency $\neq$ good performance! In fact, these two qualities are often (but not always) to the detriment of each other. Instrument and test your code before drawing conclusions, and make decisions on your allocation scheme based on your specific combination of hardware, operational requirements and test results!

## Thread safety
The TLSF allocator was not originally designed for multithreaded applications, and `tlsf_resource` is not thread-safe. Instead, `synchronized_tlsf_resource` should be used. It has a very similar implementation, but uses a naive lock during allocation and deallocation, and as such it is very simple at the cost of potential performance from more finely-grained locking strategies. 

## Placement new and `memalign`
Placement new is a language feature that enables the construction of objects at a specific location in heap memory (or in terms of the C++ abstract machine, the 'free store'). 

## Memory exhaustion
As this is a pool-based memory resource, the amount of memory available is fixed and determined upon initialization. When the pool is exhausted, the memory resource defers to a secondary memory resource to satisfy further allocations. The default resource is `std::pmr::null_memory_resource`, which simply throws `std::bad_alloc` if an allocation is attempted. In other words, if the pool is exhausted the default behavior is to throw a failure to allocate exception upon further allocation attempts. This behavior can be changed by simply providing another memory resource during allocation. 

## Installation
A CMakeLists.txt file is provided for easy usage. Start by adding this repository to your project in the manner of your choosing (e.g. git submodule, FetchContent, etc).

Then link against it using `target_link_libraries`. 
```cmake
target_link_libraries(
    <your-project>
    tlsf_resource
    ...
)
```
