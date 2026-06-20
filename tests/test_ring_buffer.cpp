#include <gtest/gtest.h>
#include <spsc_ring_buffer/SpScRingBuffer.h>
#include <ranges>
#include <vector>

namespace spsc = spsc_ring_buffer;

//////////////////////////////////////////////////////////////////////
// NOTE: Using GoogleTest:
// The point of embedding GoogleTest as a submodle in this project
// is so that you can run tests without internet connectivity, or
// have to go through the installation process. However:
// 
// 1. To check for updates to GoogleTest, do the following:
// git fetch --recurse-submodules
// git diff HEAD..origin/main -- tests/googletest
//
// 2. To apply GoogleTest updates, do the following:
// git submodule update --remote --merge tests/googletest
//////////////////////////////////////////////////////////////////////

template <typename T, size_t Capacity>
void PopulateQueue(spsc::SpScRingBuffer<T, Capacity>& ringBuffer, const std::vector<T>& values) {

  int numValues = static_cast<int>(values.size());
  
  ASSERT_GE(Capacity, static_cast<size_t>(numValues)) 
    << std::format("Vector size {} exceeds ring buffer capacity {}", numValues, Capacity);

  for (int i : std::views::iota(0, numValues)) {
    EXPECT_TRUE(ringBuffer.tryPush(values[i])) 
      << std::format("Populate helper failed to insert value at index: {}", i);
  }
}

TEST(RingBufferTestSuite, RingBufferDoesntGrowPastSizeLimitTest) {
  constexpr size_t capacity = 16;
  spsc::SpScRingBuffer<int, capacity> testRingBuffer;
  for (int i : std::views::iota(0, capacity)) {
    EXPECT_TRUE(testRingBuffer.tryPush(0)) << "tryPush failed at index: " << i;
  }
  EXPECT_FALSE(testRingBuffer.tryPush(0));
}

TEST(RingBufferTestSuite, CantPopFromRingBufferPastSizeLimitTest) {
  constexpr size_t capacity = 16;
  spsc::SpScRingBuffer<int, capacity> testRingBuffer;
  for (int i : std::views::iota(0, capacity)) {
    EXPECT_TRUE(testRingBuffer.tryPush(0)) << "tryPush failed at index: " << i;
  }
  EXPECT_FALSE(testRingBuffer.tryPush(0));
}