#include "common/phase_timer.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <thread>  // NOLINT

#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace google::spanner::emulator::phase_timer {
namespace {

struct Totals {
  int64_t count = 0;
  int64_t nanos = 0;
};

absl::Mutex mu;
int64_t last_record_nanos ABSL_GUARDED_BY(mu) = 0;
std::map<std::string, Totals>* totals ABSL_GUARDED_BY(mu) = nullptr;

void PrintLoop() {
  // Prints and resets the totals once no scope has finished for a second, so a
  // burst of one kind of request reads as one block.
  while (true) {
    absl::SleepFor(absl::Milliseconds(250));
    std::map<std::string, Totals> snapshot;
    {
      absl::MutexLock lock(&mu);
      if (totals->empty() || NowNanos() - last_record_nanos < 1000000000) {
        continue;
      }
      snapshot.swap(*totals);
    }
    std::fprintf(stderr, "=== phase timers ===\n");
    for (const auto& [name, t] : snapshot) {
      std::fprintf(stderr, "PHASE %-48s n=%-8lld mean_us=%10.1f total_ms=%10.1f\n",
                   name.c_str(), static_cast<long long>(t.count),
                   t.nanos / 1e3 / t.count, t.nanos / 1e6);
    }
    std::fflush(stderr);
  }
}

bool ComputeEnabled() {
  const char* v = std::getenv("SPANNER_EMULATOR_PHASE_TIMERS");
  if (v == nullptr || std::string(v) != "1") return false;
  {
    absl::MutexLock lock(&mu);
    totals = new std::map<std::string, Totals>();
  }
  std::thread(PrintLoop).detach();
  return true;
}

}  // namespace

bool Enabled() {
  static const bool enabled = ComputeEnabled();
  return enabled;
}

void Record(const char* name, int64_t nanos) {
  absl::MutexLock lock(&mu);
  Totals& t = (*totals)[name];
  ++t.count;
  t.nanos += nanos;
  last_record_nanos = NowNanos();
}

}  // namespace google::spanner::emulator::phase_timer
