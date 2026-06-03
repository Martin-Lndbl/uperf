#pragma once

// TODO: Distinguish between intel and amd. Currently, only amd(zen3-5) is
// supported

#if defined(__x86_64__) || defined(_M_X64)
#define ARCH_TARGET_X86_64
#elif defined(__aarch64__) || defined(_M_ARM64)
#define ARCH_TARGET_ARM64
#else
#error Unsuported target architecture
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef ARCH_TARGET_X86_64

inline void enable_pmu() {
  // TODO: This is currently a noop as the pmu is usually enabled by default
}

inline void start_msr(int32_t msr) {
  // TODO: This is currently merged with write_msr.
}

inline void write_msr(uint32_t msr, uint64_t value) {
  asm volatile("wrmsr"
               :
               : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

inline uint64_t read_msr(uint32_t msr) {
  uint32_t low, high;
  asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
  return ((uint64_t)high << 32) | low;
}

#elifdef ARCH_TARGET_ARM64

// TODO

#endif

namespace uperf {

// ---------------------------------------
// Declaration of required data structures
// ---------------------------------------

// Some architecture differentiate between different classes of pmcs.
enum PMClass {
#ifdef ARCH_TARGET_X86_64
  // Specifies on-core AMD events
  CORE,
  // Specifies Northbridge events to be counted and controls other aspects of
  // counter operation. Support for these MSRs is indicated by CPUID
  // Fn8000_0001_ECX.PerfCtrExtNB = 1
  NORTHBRIDGE,
  // Specifies the L2 cache events to be counted and controls other aspects of
  // counter operation. Support for these MSRs is indicated by CPUID
  // Fn8000_0001_ECX.PerfCtrExtL2I = 1.
  L2I,
#elifdef ARCH_TARGET_ARM64
#endif
};

// A Performance Measurement Counter (PMC) represents a physical counter and
// corresponding configuration registers.
struct PMC {
  // Whether or not this pmc is currently counting
  std::unique_ptr<std::atomic<bool>> free;

  // For the corresponding performance counter, this register specifies the
  // events counted, and controls other aspects of counter operation.
  uint32_t perfEvtSel;

  // Used to count specific processor events, or the duration of events,
  // as specified by the corresponding PerfEvtSel[n] register.
  uint32_t perfCtr;

  PMClass pmClass;

  PMC(uint32_t perfEvtSel, uint32_t perfCtr, PMClass pmClass)
      : perfEvtSel(perfEvtSel), perfCtr(perfCtr), pmClass(pmClass),
        free(nullptr) {}

  PMC(PMC const &pmc)
      : perfEvtSel(pmc.perfEvtSel), perfCtr(pmc.perfCtr), pmClass(pmc.pmClass),
        free(std::make_unique<std::atomic<bool>>(true)) {}

  uint64_t probe() { return read_msr(perfCtr); }

  void configure(uint64_t bitmap) {
    write_msr(perfCtr, 0);
    write_msr(perfEvtSel, bitmap);
  }

  void start(){};
  void stop() { write_msr(perfEvtSel, 0); };
};

// TODO: Extend this logic to support sharing counters between groups of cores
struct PMCSelect {
  PMCSelect(std::initializer_list<PMC> pmcs) : pmcs(pmcs) {}

  PMC *acquire(PMClass pmClass) {
    for (auto &pmc : pmcs) {
      bool expected = true;
      if (pmc.pmClass == pmClass &&
          pmc.free->compare_exchange_strong(expected, false))
        return &pmc;
    }

    std::cerr << "Couldn't acquire PMC of the requested class. Retrying..."
              << std::endl;
    return acquire(pmClass);
  }

  void release(PMC *pmc) { pmc->free->store(true); }

private:
  std::vector<PMC> pmcs;
};

inline PMCSelect pmcSelect{
#ifdef ARCH_TARGET_X86_64
    PMC{0xC0010200, 0xC0010201, CORE}, PMC{0xC0010202, 0xC0010203, CORE},
    PMC{0xC0010204, 0xC0010205, CORE}, PMC{0xC0010206, 0xC0010207, CORE},
    PMC{0xC0010208, 0xC0010209, CORE}, PMC{0xC001020A, 0xC001020B, CORE},
#elifdef ARCH_TARGET_ARM64
#endif
};

typedef enum : uint64_t {
#ifdef ARCH_TARGET_X86_64
  // To be applied to CORE event registers
  CYCLES = 0x430076,
  // To be applied to CORE event registers
  INSTRUCTIONS = 0x4300C0,
  // To be applied to CORE event registers
  CACHE_MISSES = 0x430964,
  // To be applied to CORE event registers
  BRANCH_MISSES = 0x4300C3,
  // To be applied to CORE event registers
  TLB_FLUSHES = 0x43FF78,
  // To be applied to CORE event registers
  DATA_CACHE_ACCESSES = 0x430729,
#elifdef ARCH_TARGET_ARM64
#endif
} EventSelect;

// ---------------------------------------
// High level measurement logic
// ---------------------------------------

struct Event {
  std::string name;

  PMClass pmClass;
  uint64_t bitmap;

  uint64_t before;
  uint64_t after;

  Event(const std::string &name, PMClass pmClass, uint64_t bitmap)
      : name(name), pmClass(pmClass), bitmap(bitmap) {}

  void start() {
    pmc = corePMCs.acquire(pmClass);
    pmc->configure(bitmap);
    pmc->start();
    before = pmc->probe();
  }

  void stop() {
    after = pmc->probe();
    pmc->stop();
    corePMCs.release(pmc);
  }

  uint64_t report() { return after - before; }

private:
  PMCSelect &corePMCs{pmcSelect};
  PMC *pmc;
};

struct Collection {
  std::vector<Event> events;
  std::chrono::time_point<std::chrono::steady_clock> startTime;
  std::chrono::time_point<std::chrono::steady_clock> stopTime;

  Collection() {
    registerCounter("cycles", CORE, CYCLES);
    registerCounter("instructions", CORE, INSTRUCTIONS);
    registerCounter("cache-misses", CORE, CACHE_MISSES);
    registerCounter("branch-misses", CORE, BRANCH_MISSES);
    registerCounter("tlb-flushes", CORE, TLB_FLUSHES);
    registerCounter("cache-accesses", CORE, DATA_CACHE_ACCESSES);
  }

  void registerCounter(const std::string &name, PMClass pmClass,
                       uint64_t bitmap) {
    events.push_back(Event(name, pmClass, bitmap));
  }

  double getCounter(const std::string &name) {
    for (auto &event : events) {
      if (event.name == name)
        return event.report();
    }
    return -1;
  }

  double getDuration() {
    return std::chrono::duration<double>(stopTime - startTime).count();
  }

  double getIPC() { return getCounter("instructions") / getCounter("cycles"); }

  static void printCounter(std::ostream &headerOut, std::ostream &dataOut,
                           std::string name, std::string counterValue,
                           bool addComma = true) {
    auto width = std::max(name.length(), counterValue.length());
    headerOut << std::setw(static_cast<int>(width)) << name
              << (addComma ? "," : "") << " ";
    dataOut << std::setw(static_cast<int>(width)) << counterValue
            << (addComma ? "," : "") << " ";
  }

  template <typename T>
  static void printCounter(std::ostream &headerOut, std::ostream &dataOut,
                           std::string name, T counterValue,
                           bool addComma = true) {
    std::stringstream stream;
    stream << std::fixed << std::setprecision(2) << counterValue;
    printCounter(headerOut, dataOut, name, stream.str(), addComma);
  }

  void printReport(std::ostream &out, uint64_t normalizationConstant) {
    std::stringstream header;
    std::stringstream data;
    printReport(header, data, normalizationConstant);
    out << header.str() << std::endl;
    out << data.str() << std::endl;
  }

  void printReport(std::ostream &headerOut, std::ostream &dataOut,
                   uint64_t normalizationConstant) {
    if (!events.size())
      return;

    printCounter(headerOut, dataOut, "duration", getDuration());
    for (unsigned i = 0; i < events.size(); i++) {
      printCounter(headerOut, dataOut, events[i].name,
                   events[i].report() /
                       static_cast<double>(normalizationConstant));
    }

    printCounter(headerOut, dataOut, "scale", normalizationConstant);

    printCounter(headerOut, dataOut, "IPC", getIPC());
  }

  void startCounters() {
    for (auto &event : events) {
      event.start();
    }
    startTime = std::chrono::steady_clock::now();
  }

  void stopCounters() {
    stopTime = std::chrono::steady_clock::now();
    for (auto &event : events) {
      event.stop();
    }
  }
};
} // namespace uperf
