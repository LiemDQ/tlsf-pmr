#include <gtest/gtest.h>
#include "pool.hpp"

using namespace tlsf;

class PoolTests : public ::testing::Test {
    protected:
        PoolTests(): pool(1024*1024) {}
        tlsf_pool pool;    
};



TEST_F(PoolTests, poolAllocatesOnConstruction){
    EXPECT_TRUE(pool.is_allocated());
}

TEST_F(PoolTests, poolMalloc){
    using namespace tlsf::detail;
    void* bytes_1024 = pool.malloc_pool(1024);
    block_header* header = block_header::from_void_ptr(bytes_1024);
    ASSERT_EQ(header->get_size(), 1024);
    EXPECT_TRUE(bytes_1024);
    EXPECT_TRUE(pool.free_pool(bytes_1024));
    void* bytes_1024_2 = pool.malloc_pool(1024);
    EXPECT_TRUE(bytes_1024_2);
    EXPECT_TRUE(pool.free_pool(bytes_1024_2));
    
    void* bytes_1MB = pool.malloc_pool(1024*1024/2);
    header = block_header::from_void_ptr(bytes_1MB);
    ASSERT_EQ(header->get_size(), 1024*1024/2);
    EXPECT_TRUE(bytes_1MB);
    pool.free_pool(bytes_1MB);
    
    void* bytes_toomany = pool.malloc_pool(1024*1024 + 1);
    EXPECT_FALSE(bytes_toomany);
}

TEST(PoolDeathTest, poolDeallocatesOnDestruction){
    {
        tlsf_pool pool(1024*1024);
        void* bytes = pool.malloc_pool(2048);
        EXPECT_TRUE(pool.free_pool(bytes));
        void* bytes2 = pool.memalign_pool(2048, 32);
        EXPECT_TRUE(pool.free_pool(bytes2));
    }

    {
        tlsf_pool pool(1024*1024);
    }
}


