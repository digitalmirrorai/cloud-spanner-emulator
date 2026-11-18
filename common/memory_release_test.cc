#include "common/memory_release.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>

#include "gtest/gtest.h"
#include "tcmalloc/malloc_extension.h"

namespace google {
namespace spanner {
namespace emulator {
namespace memory {
namespace {

size_t PhysicalBytes() {
  return tcmalloc::MallocExtension::GetNumericProperty(
             "generic.physical_memory_used")
      .value_or(0);
}

TEST(MemoryReleaseTest, ReleaseFreeMemoryReturnsFreedPagesToTheSystem) {
  if (!tcmalloc::MallocExtension::NeedsProcessBackgroundActions()) {
    GTEST_SKIP() << "Not linked against tcmalloc";
  }
  // Allocate, touch and free 256 MiB in 64 KiB blocks, as rows and their
  // values are freed when a database is dropped.
  {
    std::vector<std::unique_ptr<char[]>> blocks;
    for (int i = 0; i < 4096; ++i) {
      blocks.push_back(std::make_unique<char[]>(64 << 10));
      std::memset(blocks.back().get(), 1, 64 << 10);
    }
  }
  const size_t held = PhysicalBytes();
  ReleaseFreeMemory();
  const size_t after = PhysicalBytes();
  EXPECT_LT(FreeBytesHeld(), size_t{4} << 20);
  EXPECT_GE(held - after, size_t{128} << 20)
      << "held " << held << " bytes, " << after << " after the release";
}

}  // namespace
}  // namespace memory
}  // namespace emulator
}  // namespace spanner
}  // namespace google
