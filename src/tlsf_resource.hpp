#pragma once
#include <memory_resource>
#include <cstddef>
#include "pool.hpp"

namespace tlsf {

/**
 * @brief Two-level segregated fit memory allocator and memory resource. 
 * 
 * 
 * 
 */
class tlsf_resource : public std::pmr::memory_resource {

    public:

        explicit tlsf_resource(std::size_t size) : memory_pool(size) {}
        explicit tlsf_resource() noexcept: memory_pool() {}
        explicit tlsf_resource(std::size_t size, std::pmr::memory_resource* upstream): memory_pool(size), upstream(upstream) {
            
        }
        explicit tlsf_resource(pool_options options): memory_pool(options) {}
        
        explicit tlsf_resource(const tlsf_resource& resource) noexcept: memory_pool(resource.memory_pool) {}
        inline std::pmr::memory_resource* upstream_resource() const { return this->upstream; }

    private:
        

        //overridden functions    
        void* do_allocate(std::size_t bytes, std::size_t alignment) override;
        void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override;

        bool do_is_equal(const tlsf_resource& other) const noexcept;
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

        tlsf_pool memory_pool;   
        std::pmr::memory_resource* upstream = std::pmr::null_memory_resource();
};

} //namespace tlsf