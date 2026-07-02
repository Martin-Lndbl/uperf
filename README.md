# uPerf
### A header-only C++ wrapper for hardware performance counters on selected architectures

### Scope
Developed for unikernels. General purpose operating systems do not grant direct access to the hardware counters required for this header to work. The header further makes the following assumptions:

- Once `PerfEvent` is constructed, the active thread remains on the same core for the duration of the measurement
    - I.e. Threads are pinned to cores
- No other entity writes to the hardware counters on this core
    - I.e. Only a single instance of `PMCSelect` per core (see below for more information)

### Basic Usage:

```c++
#include "uperf.hpp"
...
PerfEvent e;
e.startCounters();
yourBenchmark();
e.stopCounters();
e.printReport(std::cout, n); // use n as scale factor
```

This prints something like this:
```
cycles, instructions, LLC-misses, branch-misses,    scale,  IPC 
 10.97,        28.01,       0.00,          0.00, 10000000, 2.55
```

### Advanced Usage:
#### Disable pre-configured events
By default, a fixed set of events is registered on construction of `PerfEvent`.
To disable this behavior, pass `false` to the constructor. You may then register custom events. 
```c++
#include "uperf.hpp"
...
PerfEvent e{false};
e.registerCounter(uperf::PERF_COUNT_HW::CPU_CYCLES);
e.startCounters();
yourBenchmark();
e.stopCounters();
e.printReport(std::cout, n); // use n as scale factor
```

This prints something like this:
```csv
duration, cpu-cycles,   scale, IPC,
    0.00,       1.00, 1048576, nan,
```

> [!NOTE]
> IPC only shows a valid number if instructions and cpu-cycles are measured

#### Bring your own events
As seen above, the function `registerCounter(...)` can be used to register additional counters.

> [!IMPORTANT]
> The header does not multiplex counters.
> Registering more events than hardware counters are available will result in some events being dropped.
> The header prints a warning to stdout if this happens.

If you know the hardware event code of your event, you may also use a function overload of `registerCounter`:
```c++
// Equivalent to: registerCounter(uperf::PERF_COUNT_HW::CPU_CYCLES);
registerCounter(0x430076, uperf::CORE, "cpu-cycles");
```
The `uperf::CORE` tells uperf which counter class to use. This enables support for uncore counters later on.

#### Share counters between threads
Multiple threads may share the available hardware counters on a core. For this, each thread's `PerfEvent` must reference
the same `PMCSelect` object. The easiest way to achieve this is to instantiate `PerfEvent` on a single thread first and
pass the created `PMCSelect` object in the `PerfEvent` constructor of subsequently spawned threads:
```c++
// In the context of t1:
PerfEvent perf_t1{false};
// In the context of t2:
PerfEvent perf_t2{perf_t1.pmcs};

// t1 and t2 can now register events and start/stop measurements independently
```

> [!NOTE]
> This feature becomes more useful in the context of uncore counters (currently not fully supported),
> as the number of hardware counter per core is limited enough without sharing them between threads.

### Usage of PerfEventBlock (convenience wrapper):

```c++
#include <cstdint>
#include <osv/power.hh>
#include <osv/uperf.hh>

constexpr uint64_t n = 1ul << 20;

extern "C" void osv_app_main() {
  PerfEvent perf;
  BenchmarkParameters params;
  for (uint64_t reps{0}; reps < 4; ++reps) {
    // Add index of current run to output
    params.setParam("run", reps);
    // Counters are started in constructor
    PerfEventBlock perfBlock(perf, n, params, reps == 0);
    // Your benchmark goes here
    for (uint64_t i{0}; i < n; ++i) {
      asm volatile("" ::: "memory");
    }
    // Counters are automatically stopped and printed on destruction of perfBlock
  }
  osv::poweroff();
}
```

This prints something like this:
```csv
run, time sec, micros, millis, duration, cpu-cycles, instructions, l2-cache-misses, branch-misses,   scale,  IPC,
  0,     0.00,    480,   0.48,     0.00,       1.01,         2.00,            0.00,          0.00, 1048576, 1.98,
  1,     0.00,    437,   0.44,     0.00,       1.01,         2.00,            0.00,          0.00, 1048576, 1.99,
  2,     0.00,    452,   0.45,     0.00,       1.01,         2.00,            0.00,          0.00, 1048576, 1.98,
  3,     0.00,    437,   0.44,     0.00,       1.01,         2.00,            0.00,          0.00, 1048576, 1.99,
...
```

### Supported architectures
We only list architectures we tested on. Similar ones may work sufficiently as well
#### x86
- Zen 4
- Zen 5
- Intel Skylake
#### ARM
- Ampere 1a
- Neoverse V-1
- Neoverse V-2
