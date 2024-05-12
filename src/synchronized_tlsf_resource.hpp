#pragma once

#include <memory_resource>
#include "pool.hpp"
#include <mutex>

namespace tlsf {

/**
 * @brief Thread-safe implementation of the two-level segregated fit memory allocator and memory resource, using the `std::pmr` API. 
 * The difference between this and `tlsf_resource` is that a mutex is held during allocation and deallocation. 
 * 
 * @warning `synchronized_tlsf_resource` does not guarantee that the upstream memory resource is thread-safe. It can only guarantee 
 * that _accessing_ the upstream resource via allocation calls to the _same_ `synchronized_tlsf_resource` are thread-safe. For example, 
 * if there are two `synchronized_tlsf_resource` instances on different threads using the same upstream resource, there is no guarantee 
 * that their allocation calls will be thread-safe unless the upstream resource is also thread-safe. 
 * 
 * @warning This is a stateful resource and it must outlive any objects whose memory is allocated by it! Failure to do so will result in dangling pointers.
 * 
 * @note While the locking strategy employed here is very simple, keep in mind that any kind of mutual exclusion will undermine the execution determinancy provided 
 * by the TLSF allocation scheme. The extent to which this matters in practice depends on your specific application and requirements. It may be advisable to instead use a separate
 * `tlsf_resource` for each thread, while ensuring the upstream resource (if any) is thread-safe.
 */
class synchronized_tlsf_resource: public std::pmr::memory_resource {
       public:

        explicit synchronized_tlsf_resource(std::size_t size) : memory_pool(size) {}
        explicit synchronized_tlsf_resource() noexcept: memory_pool() {}
        explicit synchronized_tlsf_resource(std::size_t size, std::pmr::memory_resource* upstream): memory_pool(size), upstream(upstream) {}
        explicit synchronized_tlsf_resource(pool_options options): memory_pool(options), upstream(options.upstream_resource) {}
        explicit synchronized_tlsf_resource(pool_options options, std::pmr::memory_resource* upstream): memory_pool(options), upstream(upstream) {}
        explicit synchronized_tlsf_resource(const synchronized_tlsf_resource& resource) noexcept: memory_pool(resource.memory_pool) {}
        
        inline std::pmr::memory_resource* upstream_resource() const { return this->upstream; }

    private:

        //overridden functions    
        void* do_allocate(std::size_t bytes, std::size_t alignment) override;
        void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override;

        bool do_is_equal(const synchronized_tlsf_resource& other) const noexcept;
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

        tlsf_pool memory_pool;   
        std::mutex mutex;
        std::pmr::memory_resource* upstream = std::pmr::null_memory_resource();

};

}