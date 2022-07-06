#include "tlsf_resource.hpp"

namespace tlsf {

void* tlsf_resource::do_allocate(std::size_t bytes, std::size_t align) {
    //TODO: look into whether the second argument (alignment) matters at all
    //block allocation has its own alignment size, and the requested alignment does not matter.
    void* ptr = this->memory_pool.malloc(bytes);

    //if nullptr is returned, allocation has failed. Defer to upstream resource. 
    if (ptr == nullptr && bytes > 0) {
        return this->upstream->allocate(bytes, align);
    }
    return ptr;
}

void tlsf_resource::do_deallocate(void* p, std::size_t bytes, std::size_t align ){
    //The size to be deallocated is already known in the block, so the byte count and 
    //alignment values are not needed.
    if (!this->memory_pool.free(p)){
        this->upstream->deallocate(p, bytes, align);
    }
}

bool tlsf_resource::do_is_equal(const tlsf_resource& other) const noexcept {
    return this->memory_pool == other.memory_pool;
}

/**
 * @brief Determine whether the two memory resources point to the same memory pool. For upcasted pointers,
 * this requires RTTI in order to make a determination, as the underlying resource needs to be a tlsf_resource
 * in order for the comparison to be meaningful. If RTTI is disabled, then this always returns false.
 * 
 * @param other  
 * @return Whether the resources point to the same memory pool. If the other resource is not a tlsf resource, returns false.
 */
bool tlsf_resource::do_is_equal(const std::pmr::memory_resource& other) const noexcept {
    #ifdef __GXX_RTTI
        const auto cast = dynamic_cast<const tlsf_resource*>(&other);
        if (cast){
            return this->memory_pool == cast->memory_pool;
        } else 
            return false;
    #else
        return false;
    #endif
}

} //namespace tlsf