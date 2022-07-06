#include <gtest/gtest.h>
#include "tlsf_resource.hpp"
#include <vector>
#include <memory>
#include <memory_resource>

using TVal = int;

class TLSFVectorTests: public ::testing::Test {
    protected:
    TLSFVectorTests(): resource(sizeof(TVal)*5000) {
        std::pmr::polymorphic_allocator<TVal> alloc(&resource);
        vec = std::pmr::vector<TVal>(alloc);
    }
    tlsf::tlsf_resource resource;
    std::pmr::vector<TVal> vec;
};

TEST_F(TLSFVectorTests, rawAllocation) {
    int num_bytes = sizeof(long);
    int align = alignof(long);
    void* mem = resource.allocate(num_bytes, align);
    resource.deallocate(mem, num_bytes, align);
}

TEST_F(TLSFVectorTests, vectorAllocation){
    for (int i = 0; i < 2500; i++){
        vec.push_back(i);
    }
    ASSERT_EQ(vec.size(), 2500);
}

TEST_F(TLSFVectorTests, outOfMemory){
    EXPECT_THROW(resource.allocate(6000, 4), std::bad_alloc);
    EXPECT_THROW(vec.reserve(25000), std::bad_alloc);
}