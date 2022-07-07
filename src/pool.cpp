#include "pool.hpp"
#include <stdexcept>
#include <cstddef>
#include <cstdint>
#include <climits>
#include <cassert>
#include <cstring>
#include <memory_resource>

// More ergonomic cast
#define TLSF_CAST(t, exp) ((t)(exp))

namespace tlsf {

/**
 * use builtin function to count leading zeroes of a bitmap.
 * also known as "find first bit set" or "ffs" (from left)
 * also "find last bit set" or "fls"
 */
#if defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)) \
    && defined(__GNUC_PATCHLEVEL__)

inline int tlsf_ffs(unsigned int word){
    return __builtin_ffs(word)-1;
}

template <typename T>
T tlsf_min(T a, T b){
    return a < b ? a : b;
}

template <typename T>
T tlsf_max(T a, T b){
    return a > b ? a : b;
}


inline int tlsf_fls(unsigned int word){
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

static inline int tlsf_ffs(unsigned int word){
    return tlsf_fls_generic(word & (~word +1)) -1;
}

static inline int tlsf_fls(unsigned int word){
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


static_assert(sizeof(int) * CHAR_BIT == 32);
static_assert(sizeof(size_t) * CHAR_BIT >= 32);
static_assert(sizeof(size_t) * CHAR_BIT <= 64);

using tlsfptr_t = std::ptrdiff_t; 

inline bool tlsf_pool::block_header::is_last() const { return this->get_size() == 0;}
inline bool tlsf_pool::block_header::is_free() const { return TLSF_CAST(bool, this->size & this->block_header_free_bit);}
inline bool tlsf_pool::block_header::is_prev_free() const { return TLSF_CAST(bool, this->size & this->block_header_prev_free_bit);}

void* tlsf_pool::block_header::to_void_ptr() const {
    return TLSF_CAST(void*, TLSF_CAST(unsigned char*, this) + block_start_offset);
}

tlsf_pool::block_header* tlsf_pool::block_header::from_void_ptr(const void* ptr){
    //note the intermediate conversion to char ptr is to get 1-byte displacements.
    return TLSF_CAST(block_header*, TLSF_CAST(unsigned char*, ptr)-block_start_offset);
}

/*Returns a block pointer offset from the passed ptr by the size given*/
tlsf_pool::block_header* tlsf_pool::block_header::offset_to_block(const void* ptr, std::size_t blk_size){
    //possibly could remove ptr and use block->to_void_ptr instead.
    return TLSF_CAST(block_header*, TLSF_CAST(tlsfptr_t, ptr)+blk_size);
}

tlsf_pool::~tlsf_pool(){
    //NOTE: make sure the tlsf pool outlives any objects whose memory 
    //is allocated by it! Otherwise this will result in dangling pointers.
    if (this->memory_pool){
        //TODO: enable freeing using some other function handle.
        free((void*)(this->memory_pool));
        memory_pool = nullptr;
    }
}

void tlsf_pool::initialize(std::size_t size){
    this->pool_size = size;
    //TODO: currently this allocator uses malloc to allocate memory for the pool, 
    //but we should use some kind of function pointer or template instead to use other means of memory allocation.
    this->memory_pool = (char*) malloc(size);
    
    //Create a reference null block. 
    //Pointing to this block will indicate that this block pointer is not assigned.
    block_null = block_header();
    block_null.next_free = &block_null;
    block_null.prev_free = &block_null;

    this->fl_bitmap = 0;

    //fill blockmap with block_null pointers. 
    for (int i = 0; i < this->fl_index_count; ++i){
        this->sl_bitmap[i] = 0;
        for (int j = 0; j < this->sl_index_count; ++j){

            this->blocks[i][j] = &block_null;
        }
    }

     this->create_memory_pool(this->memory_pool + block_header::block_header_overhead, size-block_header::block_header_overhead);
}

char* tlsf_pool::create_memory_pool(char* mem, std::size_t bytes){
    block_header* block;
    block_header* next;

    const std::size_t pool_bytes = align_down(bytes, this->align_size);
    if (((ptrdiff_t)mem % this->align_size) != 0){
        //memory size is not aligned
        printf("tlsf init pool: Memory size must be aligned by %u bytes.\n", (unsigned int)align_size);
        return nullptr;
    }

    if (pool_bytes < this->block_size_min || pool_bytes > this->block_size_max){
#ifdef TLSF_64BIT
            printf("Init pool: Memory size must be between 0x%x and 0x%x00 bytes.\n",
                (unsigned int)(pool_overhead+block_size_min),
                (unsigned int)(pool_overhead+block_size_max));
#else
            printf("Init pool: Memory size must be between %u and %u bytes.\n",
                (unsigned int)(pool_overhead+block_size_min),
                (unsigned int)(pool_overhead+block_size_max));
#endif
        return nullptr;
    }

    // create the main free block. Offset the start of the block slightly
    // so that the prev_phys_free_block field falls outside of the pool - 
    // it will never be used.

    block = block_header::offset_to_block((void*)mem,-(tlsfptr_t)block_header::block_header_overhead);
    block->set_size(pool_bytes);
    block->set_free();
    block->set_prev_used();
    block_insert(block);

    // split the block to create a 0-size sentinel block.
    next = block->link_next();
    next->set_size(0);
    next->set_used();
    next->set_prev_free();

    return mem;    
}

/**
 * @brief Rounds up to the next block size for allocations
 * 
 * @param size 
 * @param fli 
 * @param sli 
 */
void tlsf_pool::mapping_search(std::size_t size, int* fli, int* sli){
    if (size >= small_block_size){
        const std::size_t round = (1 << (tlsf_fls_sizet(size)-sl_index_count_log2))-1;
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
void tlsf_pool::mapping_insert(std::size_t size, int* fli, int* sli){
    int fl, sl;
    if (size < small_block_size){
        fl = 0;
        sl = TLSF_CAST(int, size) / (small_block_size/sl_index_count);
    }
    else {
        fl = tlsf_fls_sizet(size);
        sl = TLSF_CAST(int, size >> (fl-sl_index_count_log2))^(1 << sl_index_count_log2);
        fl -= (fl_index_shift-1);
    }
    *fli = fl;
    *sli = sl;
}

/**
 * @brief Adjusts request size to ensure block is aligned with align.
 * 
 * @param size the requested size of the block.
 * @param align 
 * @return std::size_t 
 */
std::size_t tlsf_pool::adjust_request_size(std::size_t size, std::size_t align){
    std::size_t adjust = 0;
    if (size){
        const std::size_t aligned = align_up(size, align);
        if (aligned < block_size_max){
            adjust = tlsf_max(aligned, block_size_min);
        }
    }
    return adjust;
}

/**
 * @brief Remove a block from the free-list and update the bitmaps.
 * 
 * 
 * @param block 
 * @param fl first index
 * @param sl second index
 */
void tlsf_pool::remove_free_block(block_header* block, int fl, int sl){
    block_header* prev = block->prev_free;
    block_header* next = block->next_free;
    assert(prev && "prev_free field cannot be null");
    assert(next && "next_free field cannot be null");
    next->prev_free = prev;
    prev->next_free = next;

    // if block is head of the free list, set new head
    if (this->blocks[fl][sl] == block){
        this->blocks[fl][sl] = next;

        //if the new head is null, clear the bitmap
        if (next == &this->block_null) {
            sl_bitmap[fl] &= ~(1U << sl);
            // if the second bitmap is empty, clear the fl bitmap
            if (!sl_bitmap[fl]) {
                fl_bitmap &= ~(1U << fl);
            }
        }

    }
}

/**
 * @brief Given the fl and sl indices, adds a block to the free-list and updates the bitmaps.
 * 
 * @param block 
 * @param fl 
 * @param sl 
 */
void tlsf_pool::insert_free_block(block_header* block, int fl, int sl){
    block_header* current = this->blocks[fl][sl];
    assert(current && "free list cannot have a null entry");
    assert(block && "cannot insert a null entry into the free list");
    block->next_free = current;
    block->prev_free = &this->block_null;
    current->prev_free = block;

    assert(block->to_void_ptr() == this->align_ptr(block->to_void_ptr(), align_size) && "block not aligned properly");


    //add block to head of list and update bitmaps
    blocks[fl][sl] = block;
    fl_bitmap |= (1U << fl);
    sl_bitmap[fl] |= (1U << sl);
}

/** @brief Finds the block closest in size given a fl and sl index
 * 
 * @param fli 
 * @param sli 
 * @return tlsf_pool::block_header* 
 */
tlsf_pool::block_header* tlsf_pool::search_suitable_block(int* fli, int* sli){
    int fl = *fli;
    int sl = *sli;

    // Search for a block in the list associated with the given fl/sl index
    unsigned int sl_map = this->sl_bitmap[fl] & (~0U << sl);
    if (!sl_map) {
        // check if there is a block located in the right index, or higher
        const unsigned int fl_map = this->fl_bitmap & (~0U << (fl+1));
        if (!fl_map){
            /* no free blocks available, memory has been exhausted. */
            return nullptr;
        }

        fl = tlsf_ffs(fl_map);
        *fli = fl;
        sl_map = sl_bitmap[fl];

    }
    assert(sl_map && "internal error - second level bitmap is null");
    sl = tlsf_ffs(sl_map);
    *sli = sl;

    return this->blocks[fl][sl];
}

/**
 * @brief Removes a block from the free-list. Free-list location is calculated from the bitmaps and the block size.
 * 
 * @param block Pointer to block to be removed
 */
void tlsf_pool::block_remove(block_header* block){
    int fl, sl;
    tlsf_pool::mapping_insert(block->get_size(), &fl, &sl);
    tlsf_pool::remove_free_block(block, fl, sl);
}

/**
 * @brief Inserts a block into the free-list. Free-list location is calculated from the bitmaps and the block size.
 * 
 * @param block 
 */
void tlsf_pool::block_insert(block_header* block){
    int fl, sl;
    tlsf_pool::mapping_insert(block->get_size(), &fl, &sl);
    this->insert_free_block(block, fl, sl);
}

bool tlsf_pool::block_can_split(block_header* block, std::size_t size){
    return block->get_size() >= sizeof(block_header)+size;
}


tlsf_pool::block_header* tlsf_pool::block_split(block_header* block, std::size_t size){
    block_header* remaining = block_header::offset_to_block(block->to_void_ptr(), size-block_header::block_header_overhead);

    const size_t remain_size = block->get_size() - (size+block_header::block_header_overhead);
    assert(remaining->to_void_ptr() == align_ptr(remaining->to_void_ptr(), align_size) 
        && "remaining block not aligned properly");
    
    assert(block->get_size() == remain_size + size + block_header::block_header_overhead);
    remaining->set_size(remain_size);
    assert(remaining->get_size() >= block_size_min && "block split with invalid (too small) size");

    block->set_size(size);
    remaining->mark_as_free();
    
    return remaining;
}

/**
 * @brief Trims off any trailing block space over size, and returns it to the pool.
 * 
 * @param block 
 * @param size 
 */
void tlsf_pool::trim_free(block_header* block, std::size_t size){
    assert(block->is_free() && "block must be free");
    if (this->block_can_split(block, size)) {
        block_header* remaining_block = this->block_split(block, size);
        block->link_next();
        remaining_block->set_prev_free();
        this->block_insert(remaining_block);
    }
}

/**
 * @brief Trims trailing block space off the end of a used block, returns it to the pool
 * 
 * @param block 
 * @param size 
 */
void tlsf_pool::trim_used(block_header* block, std::size_t size){
    assert(!block->is_free() && "block must be used.");
    if (this->block_can_split(block, size)) {
        block_header* remaining_block = this->block_split(block, size);
        remaining_block->set_prev_used();
        remaining_block = this->merge_next(remaining_block);
        this->block_insert(remaining_block);
    }    
}


tlsf_pool::block_header* tlsf_pool::trim_free_leading(block_header* block, std::size_t size){
    block_header* remaining_block = block;
    if (this->block_can_split(block, size)){
        //we want the second block
        remaining_block = this->block_split(block, size-block_header::block_header_overhead);
        remaining_block->set_prev_free();

        block->link_next();
        this->block_insert(block);
    }
    return remaining_block;
}

/**
 * @brief Combines two adjacent blocks into a single block.
 * 
 * @param prev The first memory block to be joined.
 * @param block The block to be merged with the previous block, immediately following it
 * @return tlsf_pool::block_header* A pointer to the header of the new combined block.
 */
tlsf_pool::block_header* tlsf_pool::block_coalesce(block_header* prev, block_header* block){
    assert(!prev->is_last() && "previous block can't be last");
    // leaves flags untouched
    prev->size += block->get_size() + block_header::block_header_overhead;
    prev->link_next();
    return prev;
}

/**
 * @brief Combine the block with the block before it, if it is free. 
 * 
 * @param block The block to be merged with its neighbor.
 * @return tlsf_pool::block_header*: A pointer to the header of the new combined block. If the coalescing fails,
 * returns a pointer to the original block.
 */
tlsf_pool::block_header* tlsf_pool::merge_prev(block_header* block){
    if (block->is_prev_free()){
        block_header* prev = block->prev_phys_block;
        assert(prev && "prev physical block cannot be null");
        assert(prev->is_free() && "prev block is not free even though marked as such.");
        this->block_remove(prev);
        block = this->block_coalesce(prev, block);
    }
    return block;
}

/**
 * @brief Combine the block with the block after it, if it is free. 
 * 
 * @param block The block to be merged with its neighbor.
 * @return tlsf_pool::block_header*: A pointer to the header of the new combined block. If the coalescing fails, 
 * returns a pointer to the original block.
 */
tlsf_pool::block_header* tlsf_pool::merge_next(block_header* block){
    block_header* next = block->get_next();
    assert(next && "next physical block cannot be null.");
    if (next->is_free()){
        assert(!block->is_last() && "previous block cannot be last.");
        this->block_remove(next);
        block = this->block_coalesce(block, next);
    }
    return block;
}

/**
 * @brief rounds up to power of two size 
 * 
 * @param x 
 * @param align 
 * @return constexpr std::size_t 
 */
constexpr std::size_t tlsf_pool::align_up(std::size_t x, std::size_t align){
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
constexpr std::size_t tlsf_pool::align_down(std::size_t x, std::size_t align){
    assert(0 == (align & (align -1)) && "must align to a power of two");
    return x + (x & (align -1));
}

/**
 * @brief Aligns pointer to machine word
 * 
 * @param ptr 
 * @param align 
 * @return void* 
 */
void* tlsf_pool::align_ptr(const void* ptr, std::size_t align){
    const tlsfptr_t aligned = 
        (TLSF_CAST(tlsfptr_t, ptr) + (align -1)) & ~(align-1);
    assert(0 == (align & (align-1)) && "must align to a power of two");
    return TLSF_CAST(void*, aligned);
}

/**
 * @brief /* adjusts request size to ensure block is aligned with align.
 * 
 * @param size 
 * @return tlsf_pool::block_header* 
 */
tlsf_pool::block_header* tlsf_pool::locate_free(std::size_t size){
    int fl = 0, sl = 0;
    block_header* block = nullptr;
    if (size){
        this->mapping_search(size, &fl, &sl);
        if (fl < this->fl_index_count){
            block = this->search_suitable_block(&fl, &sl);
        }
    }
    if (block){
        assert(block->get_size() >= size);
        this->remove_free_block(block, fl, sl);
    }
    return block;
}

/**
 * @brief Marks the block as used, trims excess space from it and returns a ptr to the block.
 * 
 * @param block The block to be marked
 * @param size The size of the 
 * @return void* A pointer to the block if valid, nullptr otherwise. 
 */
void* tlsf_pool::prepare_used(block_header* block, std::size_t size){
    void* p = nullptr;
    if (block){
        assert(size && "size must be non-zero");
        tlsf_pool::trim_free(block, size);
        block->mark_as_used();
        p = block->to_void_ptr();
    }
    return p;
}

/**
 * @brief Allocate continguous memory from the pool. This pointer must be returned to the pool to be freed up to avoid a memory leak.
 * 
 * @param size The amount of memory requested, in bytes.
 * @return void* A pointer to the allocated memory. Returns nullptr if memory could not be allocated. 
 */
void* tlsf_pool::malloc_pool(std::size_t size){
    const std::size_t adjust = this->adjust_request_size(size, this->align_size);
    block_header* block = this->locate_free(size);

    return this->prepare_used(block, adjust);
}

/**
 * @brief free: Deallocates memory at ptr. 
 * 
 * @param ptr 
 * @return true if the memory was successfully deallocated.
 * @return false if the memory is not part of the pool.
 */
bool tlsf_pool::free_pool(void* ptr){
    if(ptr){
        block_header* block = block_header::from_void_ptr(ptr);
        //need to ensure that the memory address is part of the memory pool
        //otherwise pool is not responsible for deallocating this ptr
        if (TLSF_CAST(char*, block) > this->memory_pool + this->pool_size 
            || TLSF_CAST(char*, block) < this->memory_pool )
                return false;
            
        assert(!block->is_free() && "block already marked as free");
        block->mark_as_free();
        block = this->merge_prev(block);
        block = this->merge_next(block);
        this->block_insert(block);
        return true;
    }
    return false;
}

/**
 * @brief Reallocates the memory block to one of size bytes.
 * 
 * @param ptr 
 * @param size 
 * @return void* Pointer to the reallocated memory block.
 */
void* tlsf_pool::realloc_pool(void* ptr, std::size_t size){
    void* p = nullptr;
    //zero-size requests are treated as freeing the block.
    if(ptr && size == 0){
        this->free_pool(ptr);
    }
    // nullptrs are treated as malloc
    else if (!ptr){
        p = this->malloc_pool(size);
    }
    else {
        block_header* block = block->from_void_ptr(ptr);
        block_header* next = block->get_next();
        
        const size_t cursize = block->get_size();
        const size_t combined = cursize + next->get_size() + block_header::block_header_overhead;
        const size_t adjust = this->adjust_request_size(size, align_size);

        assert(!block->is_free() && "Block is already marked as free.");

        /**
         * If the next block is used, or when combined with the current block, does not 
         * offer enough space, we must reallocate and copy.
         */
        if (adjust > cursize && (!next->is_free() || adjust > combined)) {
            p = this->malloc_pool(size);
            if (p) {
                const size_t minsize = tlsf_min(cursize, size);
                memcpy(p, ptr, minsize);
                this->free_pool(ptr);
            }
        }
        else {
            if (adjust > cursize) {
                this->merge_next(block);
                block->mark_as_used();
            }

            this->trim_used(block, adjust);
            p = ptr;
        }
    }
    
    return p;
}

void* tlsf_pool::memalign(std::size_t align, std::size_t size){
    
    const size_t adjust = this->adjust_request_size(size, this->align_size);
    /**
     * We must allocate an additional minimum block size bytes so that
     * if our free block will leave an alignment gap which is smaller,
     * we can trim a leading free block and release it back to the pool.
     * We must do this because the previous physical block is in use, 
     * therefore the prev_phys_block field is not valid, and we can't 
     * simply adjust the size of that block.
     */
    const size_t gap_minimum = sizeof(block_header);
    const size_t size_with_gap = this->adjust_request_size(adjust+align+gap_minimum, align);

    /**
     * if alignment is less than or equals base alignment, we're done.
     * If we requested 0 bytes, return null, as malloc does.
     */
    const size_t aligned_size = (adjust && align > this->align_size) ? size_with_gap : adjust;

    block_header* block = this->locate_free(aligned_size);
    
    static_assert(sizeof(block_header) == block_size_min + block_header::block_header_overhead);

    if (block) {
        void* ptr = block->to_void_ptr();
        void* aligned = this->align_ptr(ptr, align);
        size_t gap = TLSF_CAST(size_t, TLSF_CAST(tlsfptr_t, aligned)- TLSF_CAST(tlsfptr_t, ptr));

        // if gap size is too small, offset to next aligned boundary
        if (gap && gap < gap_minimum){
            const size_t gap_remain = gap_minimum - gap;
            const size_t offset = tlsf_max(gap_remain, align);
            const void* next_aligned = TLSF_CAST(void*, 
                TLSF_CAST(tlsfptr_t, aligned)-offset);
            
            aligned = this->align_ptr(next_aligned, align);
            gap = TLSF_CAST(size_t, 
                TLSF_CAST(tlsfptr_t, aligned) -TLSF_CAST(tlsfptr_t, ptr));
        }

        if (gap) {
            assert(gap >= gap_minimum && "gap size too small");
            block = this->trim_free_leading(block, gap);
        }
    }
    return this->prepare_used(block, adjust);
}
} //namespace tlsf