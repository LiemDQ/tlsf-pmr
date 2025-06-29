#include "synchronized_tlsf_resource.hpp"

namespace tlsf {

void* synchronized_tlsf_resource::do_allocate(std::size_t bytes, std::size_t align) {
    //if align is smaller than block alignment, any allocation 
    //will already be aligned with the desired alignment. 
    this->mutex.lock();
    void* ptr = tlsf_resource::do_allocate(bytes, align);
    this->mutex.unlock();
    return ptr;
}

void synchronized_tlsf_resource::do_deallocate(void* p, std::size_t bytes, std::size_t align ){
    //The size to be deallocated is already known in the block, so the byte count and 
    //alignment values are not needed.
    this->mutex.lock();
    tlsf_resource::do_deallocate(p, bytes, align);
    this->mutex.unlock();
}


} //namespace tlsf