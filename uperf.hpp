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

inline void msr_write(uint32_t msr, uint64_t value) {
  asm volatile("wrmsr"
               :
               : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

inline void enable_pmu() {
  // TODO: This is currently a noop as the pmu is usually enabled by default
}

inline void msr_stop(int32_t counter, uint32_t evtSel) {
  msr_write(evtSel, 0);
}

inline void msr_start_with_conf(uint32_t counter, uint32_t evtSel, uint64_t value) {
  msr_write(evtSel, value);
}

inline void msr_write_counter(uint32_t msr, uint64_t value) {
  msr_write(msr, value);
}

inline uint64_t msr_read(uint32_t msr) {
  uint32_t low, high;
  asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
  return ((uint64_t)high << 32) | low;
}

#elifdef ARCH_TARGET_ARM64

inline void enable_pmu() {
  uint64_t pmcr;

  // 1. Read PMCR_EL0, set E (Enable)
  // We could also set 0b101 to also reset the cycle counter bits but not sure if its required
  asm volatile("mrs %0, pmcr_el0" : "=r"(pmcr));
  asm volatile("msr pmcr_el0, %0" : : "r"(pmcr | 0b1));

  // 2. Enable the cycle counter via PMCNTENSET_EL0 (bit 31)
  asm volatile("msr pmcntenset_el0, %0" : : "r"(1ull << 31));

  // Ensure pipeline is synchronized before we start counting
  asm volatile("isb");
}

inline void msr_stop(int32_t counter, uint32_t evtSel) {
  // Disable counter
  asm volatile("msr pmcntenclr_el0, %0" : : "r"(1ull << 31));
}

inline void msr_start_with_conf(uint32_t counter, uint32_t evtSel, uint64_t value) {
  // Ensure the event is supported
  uint64_t pmceid;
  uint64_t cmp;
  // Without FEAT_PMUv3p1
  // if (value < 64) {
  //   cmp = value;
  //   asm volatile("mrs %0, pmceid0_el0" : "=r"(pmceid));
  // } else if (value < 128){
  //   asm volatile("mrs %0, pmceid1_el0" : "=r"(pmceid));
  //   cmp = value - 64;
  // } else {
  //   std::cout << "Warning: Requested event 0x" << std::hex << value << " could not be checked for compatibility" << std::endl;
  //   goto skipcheck;
  // }

  // With FEAT_PMUv3p1
  if (value < 0x20) {
    // The event is supported if pmceid0[value] = 1. (lower 32 bit of pmceid0)
    cmp = value;
    asm volatile("mrs %0, pmceid0_el0" : "=r"(pmceid));
  } else if (value < 0x40) {
    // The event is supported if pmceid1[value-0x20] = 1. (lower 32 bit of pmceid1)
    cmp = value - 0x20;
    asm volatile("mrs %0, pmceid1_el0" : "=r"(pmceid));
  } else if (value < 0x4000) {
    // There is no pmceid3 in 64bit mode to validate these counters
    std::cout << "Warning: Requested event 0x" << std::hex << value << " could not be checked for compatibility" << std::endl;
    goto skipcheck;
  } else if (value < 0x4020) {
    // The event is supported if pmceid0[value-0x4000] = 1. (upper 32 bit of pmceid0)
    cmp = value - 0x4000;
    asm volatile("mrs %0, pmceid0_el0" : "=r"(pmceid));
  } else if (value < 0x4040) {
    // The event is supported if pmceid1[value-0x4020] = 1. (upper 32 bit of pmceid1)
    cmp = value - 0x4020;
    asm volatile("mrs %0, pmceid1_el0" : "=r"(pmceid));
  } else {
    // There is no pmceid3 in 64bit mode to validate these counters
    std::cout << "Warning: Requested event 0x" << std::hex << value << " could not be checked for compatibility" << std::endl;
    goto skipcheck;
  }

  if(((1ull << cmp) & pmceid) == 0) {
    std::cerr << "Requested event 0x" << std::hex << value << " is not supported." << std::endl;
  }

skipcheck:

  // Write event configuration into corresponding register
  switch(evtSel) {
    case 0: asm volatile("msr pmevtyper0_el0, %0" : : "r"(value)); break;
    case 1: asm volatile("msr pmevtyper1_el0, %0" : : "r"(value)); break;
    case 2: asm volatile("msr pmevtyper2_el0, %0" : : "r"(value)); break;
    case 3: asm volatile("msr pmevtyper3_el0, %0" : : "r"(value)); break;
    case 4: asm volatile("msr pmevtyper4_el0, %0" : : "r"(value)); break;
    case 5: asm volatile("msr pmevtyper5_el0, %0" : : "r"(value)); break;
  }

  // enable counter at index counter
  asm volatile("msr pmcntenset_el0, %0" : : "r"(1ull << counter));

  // Ensure pipeline is synchronized before we start counting
  asm volatile("isb");
}

inline void msr_write_counter(uint32_t counter, uint64_t value) {
  switch(counter) {
    case 0: asm volatile("msr pmevcntr0_el0, %0" : : "r"(value)); break;
    case 1: asm volatile("msr pmevcntr1_el0, %0" : : "r"(value)); break;
    case 2: asm volatile("msr pmevcntr2_el0, %0" : : "r"(value)); break;
    case 3: asm volatile("msr pmevcntr3_el0, %0" : : "r"(value)); break;
    case 4: asm volatile("msr pmevcntr4_el0, %0" : : "r"(value)); break;
    case 5: asm volatile("msr pmevcntr5_el0, %0" : : "r"(value)); break;
  }
}

inline uint64_t msr_read(uint32_t counter) {
  uint64_t value;
  switch(counter) {
    case 0: asm volatile("mrs %0, pmevcntr0_el0" : "=r"(value)); break;
    case 1: asm volatile("mrs %0, pmevcntr1_el0" : "=r"(value)); break;
    case 2: asm volatile("mrs %0, pmevcntr2_el0" : "=r"(value)); break;
    case 3: asm volatile("mrs %0, pmevcntr3_el0" : "=r"(value)); break;
    case 4: asm volatile("mrs %0, pmevcntr4_el0" : "=r"(value)); break;
    case 5: asm volatile("mrs %0, pmevcntr5_el0" : "=r"(value)); break;
  }
  return value;
}

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
  // WARN: Not supported by KVM
  // Specifies Northbridge events to be counted and controls other aspects of
  // counter operation. Support for these MSRs is indicated by CPUID
  // Fn8000_0001_ECX.PerfCtrExtNB = 1
  NORTHBRIDGE,
  // WARN: Not supported by KVM
  // Specifies the L2 cache events to be counted and controls other aspects of
  // counter operation. Support for these MSRs is indicated by CPUID
  // Fn8000_0001_ECX.PerfCtrExtL2I = 1.
  L2I,
#elifdef ARCH_TARGET_ARM64
  // Specifies on-core ARM events
  CORE
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

  uint64_t probe() { return msr_read(perfCtr); }

  void start_with_conf(uint64_t value) {
    msr_write_counter(perfCtr, 0);
    msr_start_with_conf(perfCtr, perfEvtSel, value);
  }

  void stop() { msr_stop(perfCtr, perfEvtSel); };
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
    PMC{0, 0, CORE}, PMC{1, 1, CORE}, PMC{2, 2, CORE},
    PMC{3, 3, CORE}, PMC{4, 4, CORE}, PMC{5, 5, CORE},
#endif
};

typedef enum : uint64_t {
#ifdef ARCH_TARGET_X86_64
  // Cycle
  CPU_CYCLES = 0x430076,
  // Cycle where no operation is issued because of the frontend
  STALL_FRONTEND = 0x4300A9,
  // Instruction architecturally executed
  INST_RETIRED = 0x4300C0,
  // Predictable branch speculatively executed
  BR_PRED = 0x4300C2,
  // Mispredicted or not predicted branch speculatively executed
  BR_MIS_PRED = 0x4300C3,
  // Cache miss on last on chip cache (i.e. L2)
  L2_CACHE_MISS = 0x430964,
  // Cache access on last on chip cache (i.e. L2)
  L2_CACHE = 0x430729,
  // Number of TLB flushes
  TLB_FLUSHES = 0x43FF78,
#elifdef ARCH_TARGET_ARM64
  // Instruction architecturally executed, condition code check pass, software increment
  SW_INCR = 0x0,
  // Attributable Level 1 instruction cache refill
  L1I_CACHE_REFILL = 0x1,
  // Attributable Level 1 instruction TLB refills
  L1I_TLB_REFILL = 0x2,
  // Attributable Level 1 data cache refill
  L1D_CACHE_REFILL = 0x3,
  // Attributable Level 1 data cache access
  L1D_CACHE = 0x4,
  // Attributable Level 1 data TLB refills
  L1D_TLB_REFILL = 0x5,
  // Instruction architecturally executed, condition code check pass, load
  LD_RETIRED = 0x6,
  // Instruction architecturally executed, condition code check pass, store
  ST_RETIRED = 0x7,
  // Instruction architecturally executed
  INST_RETIRED = 0x8,
  // Exception Taken
  EXC_TAKEN = 0x9,
  // Instruction architecturally executed, condition code check pass, exception return
  EXC_RETURN = 0xA,
  // Instruction architecturally executed, condition code check pass, software change of the PC
  PC_WRITE_RETIRED = 0xC,
  // Instruction architecturally executed, immediate branch
  BR_IMMED_RETIRED = 0xD,
  // Instruction architecturally executed, condition code check pass, procedure return
  BR_RETURN_RETIRED = 0xE,
  // Instruction architecturally executed, condition code check pass, unaligned load or store
  UNALIGNED_LDST_RETIRED = 0xF,
  // Mispredicted or not predicted branch speculatively executed
  BR_MIS_PRED = 0x10,
  // Cycle
  CPU_CYCLES = 0x11,
  // Predictable branch speculatively executed
  BR_PRED = 0x12,
  // Data memory access
  MEM_ACCESS = 0x13,
  // Attributable Level 1 instruction cache access
  L1I_CACHE = 0x14,
  // Attributable Level 1 data cache write-back
  L1D_CACHE_WB = 0x15,
  // Attributable Level 2 data cache access
  L2D_CACHE = 0x16,
  // Attributable Level 2 data cache refill
  L2D_CACHE_REFILL = 0x17,
  // Attributable Level 2 data cache write-back
  L2D_CACHE_WB = 0x18,
  // Attributable Bus access
  BUS_ACCESS = 0x19,
  // Local memory error
  MEMORY_ERROR = 0x1A,
  // Operation speculatively executed
  INST_SPEC = 0x1B,
  // Bus cycle
  BUS_CYCLES = 0x1D,
  // For an odd numbered counter, increment when an overflow occurs on the preceding even-numbered counter on the same PE
  CHAIN = 0x1E,
  // Attributable Level 1 data cache allocation without refill
  L1D_CACHE_ALLOCATE = 0x1F,
  // Attributable Level 2 data cache allocation without refill
  L2D_CACHE_ALLOCATE = 0x20,
  // Instruction architecturally executed, branch
  BR_RETIRED = 0x21,
  // Instruction architecturally executed, mispredicted branch
  BR_MIS_PRED_RETIRED = 0x22,
  // No operation issued because of the frontend
  STALL_FRONTEND = 0x23,
  // No operation issued because of the backend
  STALL_BACKEND = 0x24,
  // Attributable Level 2 instruction cache access
  L2I_CACHE = 0x27,
  // Attributable Level 2 instruction cache refill
  L2I_CACHE_REFILL = 0x28,
  // Attributable Level 3 data cache allocation without refill
  L3D_CACHE_ALLOCATE = 0x29,
  // Attributable Level 3 data cache refill
  L3D_CACHE_REFILL = 0x2A,
  // Attributable Level 3 data cache access
  L3D_CACHE = 0x2B,
  // Attributable Level 3 data cache access write-back
  L3D_CACHE_WB = 0x2C,
  // Last level data cache read
  LL_CACHE = 0x36,
  // Last level data cache read miss
  LL_CACHE_MISS = 0x37,
  // Level 1 data cache read miss
  L1D_CACHE_MISS = 0x39,
  // Operation retired
  OP_COMPLETE = 0x3A,
  // Operation speculated
  OP_SPEC = 0x3B,
  // No operation sent for execution
  STALL = 0x3C,
  // No operation sent for execution on a slot because of the backend
  STALL_OP_BACKEND = 0x3D,
  // No operation sent for execution on a slot because of the frontend
  STALL_OP_FRONTEND = 0x3E,
  // No operation sent for execution on a slot
  STALL_OP = 0x3F,
  // Level 2 data cache read miss
  L2_CACHE_MISS = 0x4009,
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
    pmc->start_with_conf(bitmap);
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
    enable_pmu();

    registerCounter("cycles", CORE, CPU_CYCLES);
    registerCounter("instructions", CORE, INST_RETIRED);
    registerCounter("L2-cache-misses", CORE, L2_CACHE_MISS);
    registerCounter("branch-misses", CORE, BR_MIS_PRED);
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
