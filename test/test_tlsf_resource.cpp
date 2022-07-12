#include <gtest/gtest.h>
#include "tlsf_resource.hpp"
#include <vector>
#include <memory>
#include <memory_resource>

using TVal = int;

// utility memory resource for debugging purposes
class print_resource : public std::pmr::memory_resource {
    private:
        void* do_allocate(std::size_t bytes, std::size_t ) override {
            void* result = malloc(bytes);
            fprintf(stderr, "Allocated %u bytes at memory location %p\n", bytes, result);
            return result;
            
        }
        void do_deallocate(void* p, std::size_t , std::size_t ) override {
            fprintf(stderr, "Freeing memory at address %p\n", p);
            free(p);
        }

        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
            return false;
        }
};

print_resource print_res;

 
tlsf::pool_options options {5000*sizeof(TVal), &print_res};

class TLSFVectorTests: public ::testing::Test {
    protected:
    TLSFVectorTests() : resource(options) {
        std::pmr::polymorphic_allocator<TVal> alloc(&resource);
        vec = std::pmr::vector<TVal>(alloc);
    }
    tlsf::tlsf_resource resource;
    std::pmr::vector<TVal> vec;
};

TEST(TLSFResourceTests, deallocatesOnDestruction) {
    {
        tlsf::tlsf_resource resource(options);
    }
}

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
    EXPECT_THROW(resource.allocate(6000*sizeof(TVal), 4), std::bad_alloc);
    // EXPECT_THROW(vec.reserve(5000000), std::bad_alloc);
}