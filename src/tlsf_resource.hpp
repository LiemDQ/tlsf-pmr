#pragma once
#include <memory_resource>
#include <optional>
#include "pool.hpp"

namespace tlsf {

/**
 * @brief Two-level segregated fit memory allocator and memory resource, using the `std::pmr` API. 
 * 
 * @warning This is a stateful resource and it must outlive any objects whose memory is allocated by it! Failure to do so will result in dangling pointers.
 * 
 */
class tlsf_resource : public std::pmr::memory_resource {

    public:
    //constructors
        explicit tlsf_resource() noexcept {}
        /**
         * @brief Construct a TLSF memory resource.
         * 
         * @throws `std::runtime_error` if the memory pool is unable to initialize. 
         */
        explicit tlsf_resource(
            std::size_t size, 
            std::pmr::memory_resource* upstream = std::pmr::null_memory_resource())
            : upstream(upstream) {this->initialize_memory_pool(size);}
        
        explicit tlsf_resource(
            pool_options options, 
            std::pmr::memory_resource* upstream = std::pmr::null_memory_resource())
            : upstream(upstream) {this->initialize_memory_pool(options.size, options.upstream_resource);}
    
    //copy construction is disabled for consistency with standard library pool resources
        tlsf_resource(const tlsf_resource&) = delete;
        tlsf_resource& operator=(const tlsf_resource&) = delete;

        inline std::pmr::memory_resource* upstream_resource() const { return this->upstream; }
        
        /**
         * @brief Releases all allocated memory owned by this resource.
         * 
         * @warning This will deallocate the underlying memory pool. If there are objects 
         * with memory allocated by the pool still in scope, this will result in dangling pointers!
         */
        void release();

        /**
         * @brief Returns pool options that determine the allocation behavior of this resource.
         */
        pool_options options();

        /**
         * @brief Allocate a new memory pool for this memory resource using `options`. If `replace` is `True`, then
         * any pre-existing memory pool for this resource is deallocated in the process.
         * 
         * @warning Deallocating a memory pool while objects allocated by it 
         * are still in scope will result in dangling pointers!
         * 
         * @throws `std::runtime_error` if `replace` is `false` and there is a memory pool already allocated.
         */
        void create_memory_pool(pool_options options, bool replace = false);

    protected:
        void initialize_memory_pool(
            std::size_t size, 
            std::pmr::memory_resource* upstream = std::pmr::new_delete_resource());

        //overridden functions    
        void* do_allocate(std::size_t bytes, std::size_t alignment) override;
        void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override;

        bool do_is_equal(const tlsf_resource& other) const noexcept;
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;


        std::optional<tlsf_pool> memory_pool;   
        std::pmr::memory_resource* upstream;
};

} //namespace tlsf