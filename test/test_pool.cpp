#include <gtest/gtest.h>
#include "pool.hpp"
#include <optional>

using namespace tlsf;

class PoolTests : public ::testing::Test {
    protected:
        PoolTests(): pool(tlsf_pool::create(1024*1024)) {}
        std::optional<tlsf_pool> pool;    
};


TEST_F(PoolTests, poolAllocatesOnConstruction){
    ASSERT_TRUE(pool);
    EXPECT_TRUE(pool->is_allocated());
}

TEST_F(PoolTests, poolMalloc){
    using namespace tlsf::detail;
    void* bytes_1024 = pool->malloc_pool(1024);
    block_header* header = block_header::from_void_ptr(bytes_1024);
    ASSERT_EQ(header->get_size(), 1024);
    EXPECT_TRUE(bytes_1024);
    EXPECT_TRUE(pool->free_pool(bytes_1024));
    void* bytes_1024_2 = pool->malloc_pool(1024);
    EXPECT_TRUE(bytes_1024_2);
    EXPECT_TRUE(pool->free_pool(bytes_1024_2));
    
    void* bytes_1MB = pool->malloc_pool(1024*1024/2);
    header = block_header::from_void_ptr(bytes_1MB);
    ASSERT_EQ(header->get_size(), 1024*1024/2);
    EXPECT_TRUE(bytes_1MB);
    pool->free_pool(bytes_1MB);
    
    void* bytes_toomany = pool->malloc_pool(1024*1024 + 1);
    EXPECT_FALSE(bytes_toomany);
}

TEST(PoolDeathTest, poolDeallocatesOnDestruction){
    {
        auto pool = tlsf_pool::create(1024*1024);
        ASSERT_TRUE(pool.has_value());
        ASSERT_TRUE(pool->is_allocated());
        void* bytes = pool->malloc_pool(2048);
        EXPECT_TRUE(bytes);
        EXPECT_TRUE(pool->free_pool(bytes));
        void* bytes2 = pool->memalign_pool(2048, 32);
        EXPECT_TRUE(bytes2);
        EXPECT_TRUE(pool->free_pool(bytes2));
    }

    {
        auto pool = tlsf_pool::create(1024*1024);
    }
}


