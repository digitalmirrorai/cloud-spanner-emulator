//
// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef THIRD_PARTY_CLOUD_SPANNER_EMULATOR_COMMON_CLOCK_H_
#define THIRD_PARTY_CLOUD_SPANNER_EMULATOR_COMMON_CLOCK_H_

#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

namespace google {
namespace spanner {
namespace emulator {

// The amount by which a timestamp supplied by a client may sit ahead of this
// process's clock and still be accepted where a commit timestamp is validated.
// Zero unless SPANNER_EMULATOR_MAX_CLIENT_CLOCK_SKEW_MS asks for an allowance,
// so by default the comparison stays exactly upstream's.
//
// The emulator compares a client-supplied commit timestamp against its own
// clock. Those are the same clock only when the client shares this process's
// host: under Docker Desktop on macOS or Windows the client is on the host and
// the emulator is in a VM whose clock drifts behind it, so a timestamp the
// client took moments ago arrives in this process's future and is refused for
// no fault of the caller. Such a deployment sets the variable to the number of
// milliseconds its two clocks run apart. Reads already tolerate an hour of this
// (kMaxFutureReadDuration in frontend/handlers); writes tolerate nothing.
//
// Read once, on first use.
absl::Duration MaxClientClockSkew();

// MaxClientClockSkew's parsing, separated so it can be tested: the value above
// is read once per process and a test cannot put the variable back. `value` is
// the variable's contents, or nullptr when it is unset. Anything that is not a
// non-negative whole number of milliseconds allows nothing, as an unset
// variable does.
absl::Duration MaxClientClockSkewFromEnvValue(const char* value);

// Clock implements a strictly monotonic clock.
//
// This class provides two properties that are useful for the emulator:
//
// - The values returned from `Now()` are strictly monotonic - two calls to
//   `Now()` will never return the same value, and values returned from `Now()`
//   are always increasing. This allows us to use values from `Now()` as
//   commit timestamps.
//
// - The values returned from `Now()` are truncated to microsecond resolution.
//   This is to conform with Cloud Spanner's commit timestamps which also
//   operate at microsecond resolution.
//
// This class is thread safe.
class Clock {
 public:
  Clock();

  // Returns the current time.
  absl::Time Now() ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // Mutex to guard state below.
  absl::Mutex mu_;

  // The last value we got from the system clock.
  absl::Time last_system_time_ ABSL_GUARDED_BY(mu_);

  // The last value we handed out in a call to Clock::Now().
  absl::Time last_dispensed_time_ ABSL_GUARDED_BY(mu_);
};

}  // namespace emulator
}  // namespace spanner
}  // namespace google

#endif  // THIRD_PARTY_CLOUD_SPANNER_EMULATOR_FRONTEND_COMMON_CLOCK_H_
