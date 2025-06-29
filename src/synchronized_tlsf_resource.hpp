#pragma once

#include <memory_resource>
#include "pool.hpp"
#include "tlsf_resource.hpp"
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
class synchronized_tlsf_resource: public tlsf_resource {
    public:

    //copy construction is disabled for consistency with standard library pool resources
        synchronized_tlsf_resource(const synchronized_tlsf_resource&) = delete;
        synchronized_tlsf_resource& operator=(const synchronized_tlsf_resource&) = delete;
        
    protected:

        //overridden functions    
        void* do_allocate(std::size_t bytes, std::size_t alignment) override;
        void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override;

        std::mutex mutex;

};

}