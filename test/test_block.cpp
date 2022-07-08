#include <gtest/gtest.h>
#include "block.hpp"

using namespace tlsf::detail;

TEST(UtilityTests, bitsetArithmeticCorrect){
    EXPECT_EQ(tlsf_ffs(0), -1);
    EXPECT_EQ(tlsf_ffs(1), 0);
    EXPECT_EQ(tlsf_ffs(0x80000000), 31);
    EXPECT_EQ(tlsf_ffs(0x80008000), 15);

    EXPECT_EQ(tlsf_fls(0), -1);
    EXPECT_EQ(tlsf_fls(1), 0);
    EXPECT_EQ(tlsf_fls(0x80000008), 31);
    EXPECT_EQ(tlsf_fls(0x7FFFFFFF), 30);

#ifdef TLSF_64BIT
    EXPECT_EQ(tlsf_fls_sizet(0x80000000), 31);
    EXPECT_EQ(tlsf_fls_sizet(0x100000000), 32);
    EXPECT_EQ(tlsf_fls_sizet(0xffffffffffffffff), 63);
#endif

}

TEST(UtilityTests, alignmentCorrect){
    EXPECT_EQ(align_up(998, 8), 1000);
    EXPECT_EQ(align_down(998, 8), 992);
    EXPECT_EQ(align_up(500, 32), 512);
    EXPECT_EQ(align_down(500, 32), 480);
}

TEST(UtilityTests, bitmapMapping){
    int fli, sli;
    int size = 1000;
    //minimum block size is 256 bytes. 
    //a size request of 1000 will return a 1008 size block.
    //first level index is 2: 512-1024 bytes
    //second level index is 31: 512/32 * 31 = 496 bytes
    //512 + 496 = 1008
    mapping_search(size, &fli, &sli);
    EXPECT_EQ(fli, 2);
    EXPECT_EQ(sli, 31);

    size = 1500;
    //first level index is 3: 1024-2048
    //second level index is 15: 1024/32* 15 = 480 bytes
    //1024+480 = 1504 bytes
    mapping_search(size, &fli, &sli);
    EXPECT_EQ(fli, 3);
    EXPECT_EQ(sli, 15);
}

TEST(UtilityTests, voidPtrConversionIsReversible){
    block_header* header = new block_header;
    header->set_size(128);
    
    EXPECT_EQ(header, block_header::from_void_ptr(header->to_void_ptr()));
}

TEST(UtilityTests, alignPtrCorrect){
    size_t align = 32;
    void* ptr = (void*) 1032;
    void* aligned_ptr = (void*) 1056; //must be a multiple of 32
    auto result = align_ptr(ptr, align);
    EXPECT_EQ(result, aligned_ptr);
}