#include "block.hpp"
#include <cassert>

namespace tlsf {
namespace detail {

#define TLSF_CAST(t, exp) ((t)(exp))

/**
 * use builtin function to count leading zeroes of a bitmap.
 * also known as "find first bit set" or "ffs" (from left)
 * also "find last bit set" or "fls"
 */
#if defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)) \
    && defined(__GNUC_PATCHLEVEL__)

int tlsf_ffs(unsigned int word){
    return __builtin_ffs(word)-1;
}


int tlsf_fls(unsigned int word){
    const int bit = word ? 32 - __builtin_clz(word) : 0;
    return bit-1;
}

#else 
//generic implementation
static inline int tlsf_fls_generic(unsigned int word){
    int bit = 32;

    if (!word) bit -= 1;
    if (!(word & 0xffff0000)) { word <<= 16; bit -= 16;}
    if (!(word & 0xff000000)) { word <<= 8; bit -= 8;}
    if (!(word & 0xf0000000)) { word <<= 4; bit -= 4;}
    if (!(word & 0xc0000000)) { word <<= 2; bit -= 2;}
    if (!(word & 0x80000000)) { word <<= 1; bit -= 1;}

    return bit;
}

int tlsf_ffs(unsigned int word){
    return tlsf_fls_generic(word & (~word +1)) -1;
}

int tlsf_fls(unsigned int word){
    return tlsf_fls_generic(word)-1;
}

#endif 
/**
 * 64-bit version of TLSF fls
 */
#ifdef TLSF_64BIT
// static inline int tlsf_fls_sizet(size_t size){
//     return tlsf_fls(size);
// }

int tlsf_fls_sizet(size_t size){
    int high = (int)(size >> 32);
    int bits = 0;
    if (high) {
        bits = 32 + tlsf_fls(high);
    } 
    else {
        bits = tlsf_fls((int)size & 0xffffffff);
    }
    return bits; 
}
#else 
#define tlsf_fls_sizet tlsf_fls
#endif

/**
 * @brief rounds up to power of two size 
 * 
 * @param x 
 * @param align 
 * @return constexpr std::size_t 
 */
std::size_t align_up(std::size_t x, std::size_t align){
    assert(0 == (align & (align-1)) && "must align to a power of two");
    return (x + (align -1)) & ~(align -1);
}

/**
 * @brief Rounds down to power of two size 
 * 
 * @param x 
 * @param align 
 * @return constexpr std::size_t 
 */
std::size_t align_down(std::size_t x, std::size_t align){
    assert(0 == (align & (align -1)) && "must align to a power of two");
    return x + (x & (align -1));
}

/**
 * @brief Rounds up to the next block size for allocations
 * 
 * @param size 
 * @param fli 
 * @param sli 
 */
void mapping_search(std::size_t size, int* fli, int* sli){
    if (size >= SMALL_BLOCK_SIZE){
        const std::size_t round = (1 << (tlsf_fls_sizet(size)-SL_INDEX_COUNT_LOG2))-1;
        size += round;
    }
    mapping_insert(size, fli, sli);
}

/**
 * @brief Computes first level index (fl) and second level index (sl)
 * 
 * @param size 
 * @param fli 
 * @param sli 
 */
void mapping_insert(std::size_t size, int* fli, int* sli){
    int fl, sl;
    if (size < SMALL_BLOCK_SIZE){
        fl = 0;
        sl = TLSF_CAST(int, size) / (SMALL_BLOCK_SIZE/SL_INDEX_COUNT);
    }
    else {
        fl = tlsf_fls_sizet(size);
        sl = TLSF_CAST(int, size >> (fl-SL_INDEX_COUNT_LOG2))^(1 << SL_INDEX_COUNT_LOG2);
        fl -= (FL_INDEX_SHIFT-1);
    }
    *fli = fl;
    *sli = sl;
}

bool block_can_split(block_header* block, std::size_t size){
    return block->get_size() >= sizeof(block_header)+size;
}


block_header* block_split(block_header* block, std::size_t size){
    block_header* remaining = block_header::offset_to_block(block->to_void_ptr(), size-BLOCK_HEADER_OVERHEAD);

    const size_t remain_size = block->get_size() - (size+BLOCK_HEADER_OVERHEAD);
    assert(remaining->to_void_ptr() == align_ptr(remaining->to_void_ptr(), ALIGN_SIZE) 
        && "remaining block not aligned properly");
    
    assert(block->get_size() == remain_size + size + BLOCK_HEADER_OVERHEAD);
    remaining->set_size(remain_size);
    assert(remaining->get_size() >= BLOCK_SIZE_MIN && "block split with invalid (too small) size");

    block->set_size(size);
    remaining->mark_as_free();
    
    return remaining;
}

/**
 * @brief Aligns pointer to machine word
 * 
 * @param ptr 
 * @param align 
 * @return void* 
 */
void* align_ptr(const void* ptr, std::size_t align){
    const tlsfptr_t aligned = 
        (TLSF_CAST(tlsfptr_t, ptr) + (align -1)) & ~(align-1);
    assert(0 == (align & (align-1)) && "must align to a power of two");
    return TLSF_CAST(void*, aligned);
}

/**
 * @brief Adjusts request size upward to ensure block is aligned with align.
 * 
 * @param size the requested size of the block.
 * @param align 
 * @return std::size_t 
 */
std::size_t adjust_request_size(std::size_t size, std::size_t align){
    std::size_t adjust = 0;
    if (size){
        const std::size_t aligned = align_up(size, align);
        if (aligned < BLOCK_SIZE_MAX){
            adjust = tlsf_max(aligned, BLOCK_SIZE_MIN);
        }
    }
    return adjust;
}


/**
 * @brief Combines two adjacent blocks into a single block.
 * 
 * @param prev The first memory block to be joined.
 * @param block The block to be merged with the previous block, immediately following it
 * @return block_header* A pointer to the header of the new combined block.
 */
block_header* block_coalesce(block_header* prev, block_header* block){
    assert(!prev->is_last() && "previous block can't be last");
    // leaves flags untouched
    prev->size += block->get_size() + BLOCK_HEADER_OVERHEAD;
    prev->link_next();
    return prev;
}

/**
 * block_header methods
 */


bool block_header::is_last() const { return this->get_size() == 0;}
bool block_header::is_free() const { return TLSF_CAST(bool, this->size & BLOCK_HEADER_FREE_BIT);}
bool block_header::is_prev_free() const { return TLSF_CAST(bool, this->size & BLOCK_HEADER_PREV_FREE_BIT);}

/**
 * @brief Obtain a pointer to the raw memory inside the block, skipping past the block header.
 * 
 * @return void* 
 */
void* block_header::to_void_ptr() const {
    return TLSF_CAST(void*, TLSF_CAST(unsigned char*, this) + BLOCK_START_OFFSET);
}


/**
 * @brief Get the block pointer from the void ptr to the raw memory inside the block.
 * 
 * @param ptr 
 * @return block_header* 
 */
block_header* block_header::from_void_ptr(const void* ptr){
    //note the intermediate conversion to unsigned char ptr is to get 1-byte displacements.
    return TLSF_CAST(block_header*, TLSF_CAST(unsigned char*, ptr)-BLOCK_START_OFFSET);
}

/*Returns a block pointer offset from the passed ptr by the size given*/
block_header* block_header::offset_to_block(const void* ptr, std::size_t blk_size){
    //possibly could remove ptr and use block->to_void_ptr instead.
    return TLSF_CAST(block_header*, TLSF_CAST(tlsfptr_t, ptr)+blk_size);
}

void block_header::mark_as_free() {
    block_header* next = this->link_next();
    next->set_prev_free();
    this->set_free();
}

void block_header::mark_as_used(){
    block_header* next = this->get_next();
    next->set_prev_used();
    this->set_used();
}



block_header* block_header::get_next(){
        block_header* next = this->offset_to_block(this->to_void_ptr(), this->get_size()-BLOCK_START_OFFSET);
        assert(!this->is_last());
        return next;
    }

block_header* block_header::link_next(){
    block_header* next = this->get_next();
    next->prev_phys_block = this;
    return next;
}

}
}