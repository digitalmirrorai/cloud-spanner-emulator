#ifndef THIRD_PARTY_CLOUD_SPANNER_EMULATOR_COMMON_PHASE_TIMER_H_
#define THIRD_PARTY_CLOUD_SPANNER_EMULATOR_COMMON_PHASE_TIMER_H_

#include <cstdint>
#include <ctime>
#include <string>

namespace google::spanner::emulator::phase_timer {

// Profiling aid: when SPANNER_EMULATOR_PHASE_TIMERS=1, named scopes add their
// wall time to a per-name total that is printed to stderr, and reset, after a
// second without any scope finishing. Disabled, a scope costs one predictable branch.
bool Enabled();
void Record(const char* name, int64_t nanos);
// Adds `count` (not a duration) under a name built at run time.
void Count(const std::string& name, int64_t count);

inline int64_t NowNanos() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return int64_t{ts.tv_sec} * 1000000000 + ts.tv_nsec;
}

class Scope {
 public:
  explicit Scope(const char* name)
      : name_(name), start_(Enabled() ? NowNanos() : 0) {}
  ~Scope() {
    if (start_ != 0) Record(name_, NowNanos() - start_);
  }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

 private:
  const char* name_;
  int64_t start_;
};

}  // namespace google::spanner::emulator::phase_timer

// DM_T0(v) starts a lap clock; DM_LAP(name, v) records the time since the last
// lap under `name` and restarts the clock.
#define DM_T0(v)                                                         \
  int64_t v = ::google::spanner::emulator::phase_timer::Enabled()        \
                  ? ::google::spanner::emulator::phase_timer::NowNanos() \
                  : 0
#define DM_LAP(name, v)                                                    \
  do {                                                                     \
    if (v != 0) {                                                          \
      int64_t dm_now_ = ::google::spanner::emulator::phase_timer::NowNanos(); \
      ::google::spanner::emulator::phase_timer::Record(name, dm_now_ - v); \
      v = dm_now_;                                                         \
    }                                                                      \
  } while (0)

#define DM_PHASE_CONCAT2(a, b) a##b
#define DM_PHASE_CONCAT(a, b) DM_PHASE_CONCAT2(a, b)
#define DM_PHASE(name)                                         \
  ::google::spanner::emulator::phase_timer::Scope DM_PHASE_CONCAT( \
      dm_phase_, __LINE__)(name)

#endif  // THIRD_PARTY_CLOUD_SPANNER_EMULATOR_COMMON_PHASE_TIMER_H_
