#pragma once
#include "block.hpp"
#include <cstddef>
#include <cassert>
#include <memory_resource>

namespace tlsf {

struct pool_options {
    std::size_t size;
    std::pmr::memory_resource* upstream_resource;
};

/**
 * @brief Memory pool that allocates following the TLSF algorithm, and contains 
 * all the internal implementation details. Unless you are implementing your own memory resource 
 * or need the lower-level control, you should use `tlsf_resource` or 
 * `synchronized_tlsf_resource` instead. 
 * 
 * @warning Make sure the pool outlives any objects whose memory is allocated by it! Failure to do so will result in dangling pointers.
 */
class tlsf_pool {

    public:
        static constexpr std::size_t DEFAULT_POOL_SIZE = 1024*1024;
        
        explicit tlsf_pool(std::size_t bytes){ this->initialize(bytes); }
        explicit tlsf_pool() { this->initialize(DEFAULT_POOL_SIZE); }
        explicit tlsf_pool(pool_options options) : upstream(options.upstream_resource) 
            {this->initialize(options.size); }

        //copy construction is disabled
        tlsf_pool(const tlsf_pool&) = delete;
        tlsf_pool& operator=(const tlsf_pool&) = delete;

        ~tlsf_pool();

        void* malloc_pool(std::size_t size);
        bool free_pool(void* ptr);
        void* realloc_pool(void* ptr, std::size_t size);
        void* memalign_pool(std::size_t align, std::size_t size);

        
        inline std::pmr::memory_resource* pool_resource() const {  return this->upstream; }
        inline bool is_allocated() const { return this->memory_pool != nullptr; }
        inline bool operator==(const tlsf_pool& other) const {
            return this->memory_pool == other.memory_pool && this->memory_pool != nullptr;
        }
    
    private:
        
        using tlsfptr_t = ptrdiff_t;
       
        static constexpr std::size_t POOL_OVERHEAD = 2*detail::BLOCK_HEADER_OVERHEAD;
        static constexpr std::size_t TLSF_ALLOC_OVERHEAD = detail::BLOCK_HEADER_OVERHEAD;
        
        const inline std::size_t tlsf_size(){
            return sizeof(*this);
        }
        //reference empty block
        detail::block_header block_null;

        unsigned int fl_bitmap;
        unsigned int sl_bitmap[detail::FL_INDEX_COUNT];

        //internal block storage
        detail::block_header* blocks[detail::FL_INDEX_COUNT][detail::SL_INDEX_COUNT];

        void initialize(std::size_t size);

        char* create_memory_pool(char* pool, std::size_t bytes);
        
        void remove_free_block(detail::block_header* block, int fl, int sl);
        void insert_free_block(detail::block_header* block, int fl, int sl);        

        void block_remove(detail::block_header* block);
        void block_insert(detail::block_header* block);

        void trim_free(detail::block_header* block, std::size_t size);
        void trim_used(detail::block_header* block, std::size_t size);

        detail::block_header* search_suitable_block(int* fli, int* sli);
        detail::block_header* trim_free_leading(detail::block_header* block, std::size_t size);
        detail::block_header* merge_prev(detail::block_header* block);
        detail::block_header* merge_next(detail::block_header* block);
        detail::block_header* locate_free(std::size_t size);
        void* prepare_used(detail::block_header* block, std::size_t size);

        char* memory_pool = nullptr;
        std::size_t pool_size; //in bytes
        std::size_t allocated_size;
        
        //allocation function pointers
        std::pmr::memory_resource* upstream = std::pmr::new_delete_resource();
};

} //namespace tlsf