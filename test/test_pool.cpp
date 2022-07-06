#include <gtest/gtest.h>
#include "pool.hpp"

using namespace tlsf;

class PoolTests : public ::testing::Test {
    protected:
        PoolTests(): pool(1024*1024) {}
        tlsf_pool pool;    
};

TEST(UtilityTests, bitsetArithmeticCorrect){
    EXPECT_EQ(tlsf::tlsf_ffs(0), -1);
    EXPECT_EQ(tlsf::tlsf_ffs(1), 0);
    EXPECT_EQ(tlsf::tlsf_ffs(0x80000000), 31);
    EXPECT_EQ(tlsf::tlsf_ffs(0x80008000), 15);

    EXPECT_EQ(tlsf::tlsf_fls(0), -1);
    EXPECT_EQ(tlsf::tlsf_fls(1), 0);
    EXPECT_EQ(tlsf::tlsf_fls(0x80000008), 31);
    EXPECT_EQ(tlsf::tlsf_fls(0x7FFFFFFF), 30);

#ifdef TLSF_64BIT
    EXPECT_EQ(tlsf::tlsf_fls_sizet(0x80000000), 31);
    EXPECT_EQ(tlsf::tlsf_fls_sizet(0x100000000), 32);
    EXPECT_EQ(tlsf::tlsf_fls_sizet(0xffffffffffffffff), 63);
#endif

}

TEST_F(PoolTests, mallocBytes){
    void* bytes_1024 = pool.malloc(1024);
    EXPECT_TRUE(bytes_1024);
    pool.free(bytes_1024);
    void* bytes_1MB = pool.malloc(1024*1024);
    EXPECT_TRUE(bytes_1MB);
    pool.free(bytes_1MB);
    
    void* bytes_toomany = pool.malloc(1024*1024 + 1);
    EXPECT_FALSE(bytes_toomany);
}