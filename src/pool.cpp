#include "pool.hpp"
#include <cstddef>
#include <cstdint>
#include <climits>
#include <cassert>
#include <cstring>
#include <memory_resource>

// More ergonomic cast
#define TLSF_CAST(t, exp) ((t)(exp))

namespace tlsf {
using namespace detail;

static_assert(sizeof(int) * CHAR_BIT == 32);
static_assert(sizeof(size_t) * CHAR_BIT >= 32);
static_assert(sizeof(size_t) * CHAR_BIT <= 64);

using tlsfptr_t = std::ptrdiff_t; 


tlsf_pool::~tlsf_pool(){
    //NOTE: make sure the tlsf pool outlives any objects whose memory 
    //is allocated by it! Otherwise this will result in dangling pointers.
    if (this->memory_pool){
        //TODO: enable freeing using some other function handle.
        this->free_func((void*)(this->memory_pool));
        this->memory_pool = nullptr;
    }
}

void tlsf_pool::initialize(std::size_t size){
    this->pool_size = size;
    //TODO: currently this allocator uses malloc to allocate memory for the pool, 
    //but we should use some kind of function pointer or template instead to use other means of memory allocation.
    this->memory_pool = (char*) this->malloc_func(size);
    
    //Create a reference null block. 
    //Pointing to this block will indicate that this block pointer is not assigned.
    block_null = block_header();
    block_null.next_free = &block_null;
    block_null.prev_free = &block_null;

    this->fl_bitmap = 0;

    //fill blockmap with block_null pointers. 
    for (int i = 0; i < FL_INDEX_COUNT; ++i){
        this->sl_bitmap[i] = 0;
        for (int j = 0; j < SL_INDEX_COUNT; ++j){

            this->blocks[i][j] = &block_null;
        }
    }

     this->create_memory_pool(this->memory_pool + BLOCK_HEADER_OVERHEAD, size-BLOCK_HEADER_OVERHEAD);
}

char* tlsf_pool::create_memory_pool(char* mem, std::size_t bytes){
    block_header* block;
    block_header* next;

    const std::size_t pool_bytes = align_down(bytes, ALIGN_SIZE);
    if (((ptrdiff_t)mem % ALIGN_SIZE) != 0){
        //memory size is not aligned
        printf("tlsf init pool: Memory size must be aligned by %u bytes.\n", (unsigned int)ALIGN_SIZE);
        return nullptr;
    }

    if (pool_bytes < BLOCK_SIZE_MIN || pool_bytes > BLOCK_SIZE_MAX){
#ifdef TLSF_64BIT
            printf("Init pool: Memory size must be between 0x%x and 0x%x00 bytes.\n",
                (unsigned int)(pool_overhead+BLOCK_SIZE_MIN),
                (unsigned int)(pool_overhead+BLOCK_SIZE_MAX));
#else
            printf("Init pool: Memory size must be between %u and %u bytes.\n",
                (unsigned int)(pool_overhead+BLOCK_SIZE_MIN),
                (unsigned int)(pool_overhead+BLOCK_SIZE_MAX));
#endif
        return nullptr;
    }

    // create the main free block. Offset the start of the block slightly
    // so that the prev_phys_free_block field falls outside of the pool - 
    // it will never be used.

    block = block_header::offset_to_block((void*)mem,(tlsfptr_t)BLOCK_HEADER_OVERHEAD);
    block->set_size(pool_bytes);
    block->set_free();
    block->set_prev_used();
    this->block_insert(block);

    // split the block to create a 0-size sentinel block.
    next = block->link_next();
    next->set_size(0);
    next->set_used();
    next->set_prev_free();

    return mem;    
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

    assert(block->to_void_ptr() == align_ptr(block->to_void_ptr(), ALIGN_SIZE) && "block not aligned properly");


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
block_header* tlsf_pool::search_suitable_block(int* fli, int* sli){
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
    mapping_insert(block->get_size(), &fl, &sl);
    this->remove_free_block(block, fl, sl);
}

/**
 * @brief Inserts a block into the free-list. Free-list location is calculated from the bitmaps and the block size.
 * 
 * @param block 
 */
void tlsf_pool::block_insert(block_header* block){
    int fl, sl;
    mapping_insert(block->get_size(), &fl, &sl);
    this->insert_free_block(block, fl, sl);
}



/**
 * @brief Trims off any trailing block space over size, and returns it to the pool.
 * 
 * @param block 
 * @param size 
 */
void tlsf_pool::trim_free(block_header* block, std::size_t size){
    assert(block->is_free() && "block must be free");
    if (block->can_split(size)) {
        block_header* remaining_block = block_split(block, size);
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
    if (block->can_split(size)) {
        block_header* remaining_block = block_split(block, size);
        remaining_block->set_prev_used();
        remaining_block = this->merge_next(remaining_block);
        this->block_insert(remaining_block);
    }    
}


block_header* tlsf_pool::trim_free_leading(block_header* block, std::size_t size){
    block_header* remaining_block = block;
    if (block->can_split(size)){
        //we want the second block
        remaining_block = block_split(block, size-BLOCK_HEADER_OVERHEAD);
        remaining_block->set_prev_free();

        block->link_next();
        this->block_insert(block);
    }
    return remaining_block;
}

/**
 * @brief Combine the block with the block before it, if it is free. 
 * 
 * @param block The block to be merged with its neighbor.
 * @return tlsf_pool::block_header*: A pointer to the header of the new combined block. If the coalescing fails,
 * returns a pointer to the original block.
 */
block_header* tlsf_pool::merge_prev(block_header* block){
    if (block->is_prev_free()){
        block_header* prev = block->prev_phys_block;
        assert(prev && "prev physical block cannot be null");
        assert(prev->is_free() && "prev block is not free even though marked as such.");
        this->block_remove(prev);
        block = block_coalesce(prev, block);
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
block_header* tlsf_pool::merge_next(block_header* block){
    block_header* next = block->get_next();
    assert(next && "next physical block cannot be null.");
    if (next->is_free()){
        assert(!block->is_last() && "previous block cannot be last.");
        this->block_remove(next);
        block = block_coalesce(block, next);
    }
    return block;
}


/**
 * @brief Find a block in the block free-list that has the desired size, in bytes.
 * 
 * @param size 
 * @return tlsf_pool::block_header* A pointer to a block of memory of the requested size. 
 * If no such block could be found, returns nullptr.
 */
block_header* tlsf_pool::locate_free(std::size_t size){
    int fl = 0, sl = 0;
    block_header* block = nullptr;
    if (size){
        mapping_search(size, &fl, &sl);
        if (fl < FL_INDEX_COUNT){
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
 * @param size The size of the block
 * @return void* A pointer to the block if valid, nullptr otherwise. 
 */
void* tlsf_pool::prepare_used(block_header* block, std::size_t size){
    void* p = nullptr;
    if (block){
        assert(size && "size must be non-zero");
        this->trim_free(block, size);
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
    const std::size_t adjust = adjust_request_size(size, ALIGN_SIZE);
    block_header* block = this->locate_free(size);

    return this->prepare_used(block, adjust);
}

/**
 * @brief Deallocates memory at ptr. 
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
 * @note This method handles some edge cases of realloc:
 * - a non-zero size with a nullptr will behave like malloc
 * - a zero size with a non-null pointer will behave like free.
 * - a request that cnanot be satisfied will leave the original buffr untouched
 * - an extended buffer size will leave the newlly-allocated area with contents untouched.
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
        const size_t combined = cursize + next->get_size() + BLOCK_HEADER_OVERHEAD;
        const size_t adjust = adjust_request_size(size, ALIGN_SIZE);

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

void* tlsf_pool::memalign_pool(std::size_t align, std::size_t size){
    
    const size_t adjust = adjust_request_size(size, ALIGN_SIZE);
    /**
     * We must allocate an additional minimum block size bytes so that
     * if our free block will leave an alignment gap which is smaller,
     * we can trim a leading free block and release it back to the pool.
     * We must do this because the previous physical block is in use, 
     * therefore the prev_phys_block field is not valid, and we can't 
     * simply adjust the size of that block.
     */
    const size_t gap_minimum = sizeof(block_header);
    const size_t size_with_gap = adjust_request_size(adjust+align+gap_minimum, align);

    /**
     * if alignment is less than or equals base alignment, we're done.
     * If we requested 0 bytes, return null, as malloc does.
     */
    const size_t aligned_size = (adjust && align > ALIGN_SIZE) ? size_with_gap : adjust;

    block_header* block = this->locate_free(aligned_size);
    
    static_assert(sizeof(block_header) == BLOCK_SIZE_MIN + BLOCK_HEADER_OVERHEAD);

    if (block) {
        void* ptr = block->to_void_ptr();
        void* aligned = align_ptr(ptr, align);
        size_t gap = TLSF_CAST(size_t, TLSF_CAST(tlsfptr_t, aligned)- TLSF_CAST(tlsfptr_t, ptr));

        // if gap size is too small, offset to next aligned boundary
        if (gap && gap < gap_minimum){
            const size_t gap_remain = gap_minimum - gap;
            const size_t offset = tlsf_max(gap_remain, align);
            const void* next_aligned = TLSF_CAST(void*, 
                TLSF_CAST(tlsfptr_t, aligned) + offset);
            
            aligned = align_ptr(next_aligned, align);
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