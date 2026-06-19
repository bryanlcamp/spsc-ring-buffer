#include <gtest/gtest.h>
#include <spsc_ring_buffer/SpScRingBuffer.h>

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

// Simple canary test to verify GoogleTest and your headers are working completely offline
TEST(RingBufferTestSuite, InitializationTest) {
    // Replace this with your actual buffer initialization once verified
    SUCCEED(); 
}