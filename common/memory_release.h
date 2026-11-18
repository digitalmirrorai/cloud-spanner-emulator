#ifndef THIRD_PARTY_CLOUD_SPANNER_EMULATOR_COMMON_MEMORY_RELEASE_H_
#define THIRD_PARTY_CLOUD_SPANNER_EMULATOR_COMMON_MEMORY_RELEASE_H_

#include <cstddef>

namespace google {
namespace spanner {
namespace emulator {
namespace memory {

// The rate at which the background thread returns free memory to the system.
inline constexpr size_t kBackgroundReleaseBytesPerSecond = 32 << 20;

// Starts tcmalloc's background thread, which returns free memory to the system
// at kBackgroundReleaseBytesPerSecond and drains the caches of idle CPUs.
// Without it tcmalloc keeps every page the process ever used, so freeing a
// database shrinks nothing. Does nothing with
// SPANNER_EMULATOR_DISABLE_MEMORY_RELEASE=1, or when the process is not linked
// against tcmalloc. Call once, from main.
void StartBackgroundRelease();

// Returns all of the allocator's free memory to the system now, as after a
// database is destroyed. Costs a walk of the free pages, and a page that is
// allocated again later faults in again.
void ReleaseFreeMemory();

// The bytes the allocator holds free but has not returned to the system, or
// zero if it does not say.
size_t FreeBytesHeld();

}  // namespace memory
}  // namespace emulator
}  // namespace spanner
}  // namespace google

#endif  // THIRD_PARTY_CLOUD_SPANNER_EMULATOR_COMMON_MEMORY_RELEASE_H_
