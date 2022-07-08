#pragma once
#include <cstddef>
#include <cassert>
#include <utility>

#if INTPTR_MAX == INT64_MAX
#define TLSF_64BIT
#elif INTPTR_MAX == INT32_MAX
// 32 bit
#else
#error Unsupported bitness architecture for TLSF allocator.
#endif


namespace tlsf {
namespace detail {

struct block_header;
using tlsfptr_t = ptrdiff_t;


/**
 * CONSTANTS
 */

/**
 * Block sizes are always a multiple of 4. 
 * The two least significant bits of the size field are used to store the block status
 *  bit 0: whether the block is busy or free
 *  bit 1: whether the previous block is busy or free
 */
static constexpr std::size_t BLOCK_HEADER_FREE_BIT = 1 << 0;
static constexpr std::size_t BLOCK_HEADER_PREV_FREE_BIT = 1 << 1;

/**
 * @brief The only overhead exposed during usage is the size field. The previous_phys_block field is technically stored 
 * inside the previous block.
 */
static constexpr std::size_t BLOCK_HEADER_OVERHEAD = sizeof(std::size_t);

#ifdef TLSF_64BIT
// all allocation sizes are aligned to 8 bytes
static constexpr int ALIGN_SIZE_LOG2 = 3;
static constexpr int FL_INDEX_MAX = 32; //this means the largest block we can allocate is 2^32 bytes or about 2.1 GB
#else
// all allocation sizes are aligned to 4 bytes
static constexpr int ALIGN_SIZE_LOG2 = 2;
static constexpr int FL_INDEX_MAX = 30;
#endif

static constexpr int ALIGN_SIZE = (1 << ALIGN_SIZE_LOG2);

// log2 of number of linear subdivisions of block sizes
// values of 4-5 typical, so there will be 2^5 or 32 subdivisions.
static constexpr int SL_INDEX_COUNT_LOG2 = 5;

/**
 * Allocations of sizes up to (1 << FL_INDEX_MAX) are supported. Because we linearly subdivide the second-level lists 
 * and the minimum size block granularity is N bytes, it doesn't make sense to create first-level lists for sizes 
 * smaller than SL_INDEX_COUNT * N or (1 << (SL_INDEX_COUNT_LOG2 + log(N))) bytes, as we will be trying to split size
 * ranges into more slots than we have available. 
 * 
 * N is 4 or 8 bytes depending on whether the system is 32-bit or 64-bit, respectively.
 * 
 * We calculate the minimum threshold size, and place all blocks below that size into the 0th first-level list. 
 */

static constexpr int SL_INDEX_COUNT = (1 << SL_INDEX_COUNT_LOG2);
static constexpr int FL_INDEX_SHIFT = (SL_INDEX_COUNT_LOG2 + ALIGN_SIZE_LOG2);
static constexpr int FL_INDEX_COUNT = (FL_INDEX_MAX - FL_INDEX_SHIFT + 1);
static constexpr int SMALL_BLOCK_SIZE = (1 << FL_INDEX_SHIFT);

static constexpr std::size_t BLOCK_SIZE_MAX = static_cast<std::size_t>(1) << FL_INDEX_MAX;


/**
 * TLSF utility functions 
 * Based on the implementation described in this paper:
 * http://www.gii.upv.es/tlsf/files/spe_2008.pdf
 */

template <typename T>
T tlsf_min(T a, T b){
    return a < b ? a : b;
}

template <typename T>
T tlsf_max(T a, T b){
    return a > b ? a : b;
}

int tlsf_ffs(unsigned int word);
int tlsf_fls(unsigned int word);
int tlsf_fls_sizet(size_t size);

std::size_t align_up(std::size_t x, std::size_t align);
std::size_t align_down(std::size_t x, std::size_t align);


void mapping_search(std::size_t size, int* fli, int* sli);
void mapping_insert(std::size_t size, int* fli, int* sli);
void* align_ptr(const void* ptr, std::size_t align);
std::size_t adjust_request_size(std::size_t size, std::size_t align);

block_header* block_split(block_header* block, std::size_t size);
block_header* block_coalesce(block_header* prev, block_header* block);


/**
 * @brief TLSF memory block header.
 * 
 */
struct block_header {
    block_header* prev_phys_block;

    std::size_t size;
    
    /*The next_free and prev_free members are only valid if the block is free. 
     Otherwise, the raw user data starts immediately after the size member. This
     gives each block an overhead of 16 bytes during use, and empty blocks have a size of 32 bytes.
     */

    block_header* next_free;
    block_header* prev_free;

    inline std::size_t get_size() const { 
        //must filter out last two bits. Block size is always aligned to 4, 
        //so this will never affect the result
        return this->size & ~(BLOCK_HEADER_FREE_BIT | BLOCK_HEADER_PREV_FREE_BIT);
    }

    inline void set_size(std::size_t new_size){
        const std::size_t old_size = this->size;
        //must retain the last two bits regardless of the new size
        this->size = new_size | (old_size & (BLOCK_HEADER_FREE_BIT | BLOCK_HEADER_PREV_FREE_BIT));
    };

    bool is_last() const;
    bool is_free() const;
    bool is_prev_free() const;
    bool can_split(std::size_t size) const;
    void* to_void_ptr() const;
    static block_header* from_void_ptr(const void* ptr);
    static block_header* offset_to_block(const void* ptr, tlsfptr_t blk_size);    

    block_header* get_next();
    block_header* link_next();

    void mark_as_free();
    void mark_as_used();
    
    inline void set_free() {this->size |= BLOCK_HEADER_FREE_BIT; }
    inline void set_used() {this->size &= ~BLOCK_HEADER_FREE_BIT; }
    inline void set_prev_free() { this->size |= BLOCK_HEADER_PREV_FREE_BIT; }
    inline void set_prev_used() { this->size &= ~BLOCK_HEADER_PREV_FREE_BIT; }
/* User data starts after the size field in a used block */

};

static constexpr std::size_t BLOCK_START_OFFSET = offsetof(block_header, size) + sizeof(decltype(std::declval<block_header>().size));
static constexpr std::size_t BLOCK_SIZE_MIN = sizeof(block_header) - sizeof(decltype(std::declval<block_header>().prev_phys_block));

}
}