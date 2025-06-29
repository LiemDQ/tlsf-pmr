#include "tlsf_resource.hpp"
#include <optional>
#include <stdexcept>

namespace tlsf {

void tlsf_resource::initialize_memory_pool(std::size_t size, std::pmr::memory_resource* upstream) {
    this->memory_pool = tlsf_pool::create(size, upstream);
    
    //throw bad_alloc if allocation has failed.
    if (!this->memory_pool) {
        throw std::runtime_error("Initialization of TLSF memory pool failed. Possible reasons:\n"
            "(1): Internal allocated memory pointer is not aligned with minimum align size.\n"
            "(2): Upstream memory resource used by pool failed to allocate.\n"
            "(3): Requested pool size is incompatible with TLSF block size requirements.\n");
    }
}

void* tlsf_resource::do_allocate(std::size_t bytes, std::size_t align) {
    void* ptr;

    //if align is smaller than block alignment, any allocation 
    //will already be aligned with the desired alignment. 
    if (align <= detail::ALIGN_SIZE){
        ptr = this->memory_pool->malloc_pool(bytes);
    } else {
        ptr = this->memory_pool->memalign_pool(align, bytes);
    }

    //if nullptr is returned, allocation has failed. Defer to upstream resource. 
    if (ptr == nullptr && bytes > 0) {
        return this->upstream->allocate(bytes, align);
    }
    return ptr;
}

void tlsf_resource::release() {
    this->memory_pool.reset();
}

pool_options tlsf_resource::options() {
    if (this->memory_pool)
        return {this->memory_pool->allocation_size(), this->memory_pool->pool_resource()};
    else 
        return {0, nullptr};
}

void tlsf_resource::create_memory_pool(pool_options options, bool replace){
    if (!replace && this->memory_pool){
        throw std::runtime_error("Attempted to allocate a TLSF memory pool with a pre-existing pool.");
    } else {
        //deallocate existing pool first, if applicable
        this->release();
        this->initialize_memory_pool(options.size, options.upstream_resource);
    }
}

void tlsf_resource::do_deallocate(void* p, std::size_t bytes, std::size_t align ){
    //The size to be deallocated is already known in the block, so the byte count and 
    //alignment values are not needed.
    if (!this->memory_pool->free_pool(p)){
        this->upstream->deallocate(p, bytes, align);
    }
}

/**
 * @brief A tlsf resource is considered equal to another if they both point to the same memory pool. 
 * Because the allocation is done internally, this effectively means that a `tlsf_resource` is only equal to itself.
 * 
 * @param other 
 * @return true 
 * @return false 
 */
bool tlsf_resource::do_is_equal(const tlsf_resource& other) const noexcept {
    return this->memory_pool == other.memory_pool;
}

/**
 * @brief Determine whether the two memory resources point to the same memory pool. For upcasted pointers,
 * this requires RTTI in order to make a determination, as the underlying resource needs to be a tlsf_resource
 * in order for the comparison to be meaningful. If RTTI is disabled, then this always returns false.
 * 
 * @param other  
 * @return Whether the resources point to the same memory pool. If the other resource is not a tlsf resource, always returns false.
 */
bool tlsf_resource::do_is_equal(const std::pmr::memory_resource& other) const noexcept {
    #ifdef __GXX_RTTI
        const auto cast = dynamic_cast<const tlsf_resource*>(&other);
        return cast ? this->memory_pool == cast->memory_pool : false;
    #else
        return false;
    #endif
}

} //namespace tlsf