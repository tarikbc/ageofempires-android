#!/usr/bin/env python3
"""Run host regression checks against code from an applied FEX patch series.

Usage: python3 tools/tests/test_volatile_patches.py /path/to/patched/FEX

The harness compiles the real source fragments, not copies of their logic.
It checks memory boundaries, decoder policy, and diagnostic keys. It does not
replace Windows/FEX execution tests or a game performance test.
"""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def harness(tree):
    core = (tree / "FEXCore/Source/Interface/Core/Core.cpp").read_text()
    frontend = (tree / "FEXCore/Source/Interface/Core/Frontend.cpp").read_text()
    module = (tree / "Source/Windows/ARM64EC/Module.cpp").read_text()
    tracker = (tree / "Source/Windows/Common/InvalidationTracker.cpp").read_text()
    sample_start = core.index("uint64_t Sample = 0;", core.index("VolatileCodeStats.ShapeSampleIdx"))
    sample_end = core.index("VolatileCodeStats.ShapeSample[", sample_start)
    sample = core[sample_start:sample_end]
    guard_start = frontend.index("// Patch 0020: code in a volatile page")
    guard_end = frontend.index("LastFieldReadSize = 0;", guard_start)
    guard = frontend[guard_start:guard_end]
    exc_start = re.search(r"(?:struct ExceptionStat\s*\{|RipStat ExcTable\[)", module).start()
    exc_end = module.index("void OnDiskCacheHit(", exc_start)
    exc = module[exc_start:exc_end]
    exc_format = re.search(r'Out \+= fextl::fmt::format\([^\n]*Describe\(Exc\[i\][^\n]*;', module).group()
    rip_struct = re.search(r"struct RipStat \{.*?\n\};", module, re.S).group()
    validation_start = core.find("const auto CodeValidationSize =")
    if validation_start < 0:
        validation_start = core.index("if (Config.SMCChecks == FEXCore::Config::CONFIG_SMC_FULL || Block.ForceFullSMCDetection)")
    validation = core[validation_start:core.index("auto InvalidateCodeCond =", validation_start)] + "}\n"
    disk_start = core.index("if (Hit && !DiskCache.IsValidating()")
    disk_end = core.index("(", disk_start)
    depth = 1
    disk_end += 1
    while depth:
        depth += (core[disk_end] == "(") - (core[disk_end] == ")")
        disk_end += 1
    disk_guard = core[disk_start:disk_end] + " {"
    return r'''
#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <algorithm>
#include <cassert>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <vector>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
namespace fextl { using string = std::string; template<class T> using vector = std::vector<T>; namespace fmt = ::fmt; }
using WCHAR = char16_t;
struct UNICODE_STRING { uint16_t Length; uint16_t MaximumLength; WCHAR* Buffer; };
struct MEMORY_BASIC_INFORMATION { void* BaseAddress; void* AllocationBase; size_t RegionSize; unsigned Type; unsigned State; };
struct IMAGE_DOS_HEADER { int32_t e_lfanew; };
struct IMAGE_NT_HEADERS64 { struct { uint32_t SizeOfImage; } OptionalHeader; };
constexpr unsigned MEM_IMAGE = 0x1000000, MEM_FREE = 0x10000;
constexpr int MemoryBasicInformation = 0, MemoryMappedFilenameInformation = 2;
static uint8_t* Image;
static size_t Page;
void* NtCurrentProcess() { return nullptr; }
int NtQueryVirtualMemoryNative(void*, void* address, int kind, void* output, size_t, void*) {
  if (kind != MemoryBasicInformation) return -1;
  auto p = reinterpret_cast<uintptr_t>(address), base = reinterpret_cast<uintptr_t>(Image);
  if (p < base || p >= base + 2 * Page) return -1;
  auto* info = static_cast<MEMORY_BASIC_INFORMATION*>(output);
  info->AllocationBase = Image;
  info->BaseAddress = Image + (p >= base + Page ? Page : 0);
  info->RegionSize = Page;
  info->Type = MEM_IMAGE;
  info->State = 0x1000;
  return 0;
}
struct ModuleRec { uint64_t Base, End; fextl::string Name; };
fextl::vector<ModuleRec> Modules;
''' + function(module, "const ModuleRec* FindModule(") + r'''
namespace FEXCore {
namespace Config { constexpr int CONFIG_SMC_FULL = 2; }
namespace Utils {
constexpr uint64_t FEX_PAGE_MASK = ~uint64_t{4095}, FEX_PAGE_SIZE = 4096;
uint64_t crc32(const uint8_t* bytes, size_t size) {
  uint64_t hash = 0;
  for (size_t i = 0; i < size; ++i) hash = hash * 131 + bytes[i];
  return hash;
}
}
namespace X86Tables { struct DecodedInst { uint8_t InstSize; }; }
namespace Frontend {
struct Decoder {
  enum class DecodedBlockStatus { SUCCESS, INVALID_INST, UNIMPLEMENTED_INST, NOEXEC_INST, PARTIAL_DECODE_INST };
  struct DecodedBlocks {
    uint64_t Entry, Size;
    bool ForceFullSMCDetection;
    uint8_t FailExaminedSize;
    DecodedBlockStatus BlockStatus;
    uint64_t NumInstructions = 1;
  };
  struct DecodedBlockInformation { std::vector<DecodedBlocks> Blocks; };
};
}
}
namespace Frontend = FEXCore::Frontend;
''' + function(core, "static inline void HashMix(") + "\n" + function(core, "static inline void HashGuestRange(") + "\n" + function(core, "static uint64_t HashDecodedBlocks(") + r'''
namespace IR { enum class OpSize { i64Bit }; }
struct TestDispatcher {
  uint64_t Length = 0;
  uint64_t _EntrypointOffset(int, uint64_t address) { return address; }
  uint64_t _PatchableGuestCRC(IR::OpSize, uint64_t hash, int64_t, uint64_t) { return hash; }
  uint64_t Constant(uint64_t value) { return value; }
  uint64_t _ValidateCode(uint64_t, uint64_t, uint64_t size) { assert(size > 0); Length = size; return 0; }
};
uint64_t ValidationLength(FEXCore::Frontend::Decoder::DecodedBlocks Block) {
  uint8_t bytes[16] = {0x0f, 0x04};
  Block.Entry = reinterpret_cast<uintptr_t>(bytes);
  FEXCore::X86Tables::DecodedInst instruction {0};
  const auto* DecodedInfo = &instruction;
  uint64_t BlockInstructionsLength = Block.Size, InstAddress = Block.Entry + Block.Size, GuestRIP = Block.Entry;
  int GPRSize = 8;
  bool WantsDiskCachePatching = false;
  struct { int SMCChecks = 0; } Config;
  TestDispatcher dispatcher;
  struct { TestDispatcher* OpDispatcher; } thread {&dispatcher};
  auto* Thread = &thread;
''' + validation + r'''
  return dispatcher.Length;
}
using ULONG = uint32_t;
constexpr ULONG PAGE_READONLY=2, PAGE_READWRITE=4, PAGE_WRITECOPY=8, PAGE_EXECUTE=16;
constexpr ULONG PAGE_EXECUTE_READ=32, PAGE_EXECUTE_READWRITE=64, PAGE_EXECUTE_WRITECOPY=128;
namespace LogMan { namespace Msg { template<class... T> void DFmt(const char*, T...) {} } }
namespace FEXCore { template<class T> struct IntervalList { struct Interval { T Base, End; }; }; }
struct TestIntervals {
  bool Present = true;
  template<class T> void Insert(T) { Present = true; }
  template<class T> void Remove(T) { Present = false; }
  template<class T> bool Intersect(T) { return Present; }
};
class InvalidationTracker {
public:
  void HandleMemoryProtectionNotification(uint64_t Address, uint64_t Size, ULONG Prot);
  void ForgetTrapped(uint64_t, uint64_t) {}
  void ExecutableRangeRemoved() { ++Epoch; }
  void InvalidateIntervalInternal(uint64_t, uint64_t) { ++Invalidations; }
  struct { bool IsVolatileRange(uint64_t, uint64_t) { return true; } } CTX;
  TestIntervals XIntervals, RWXIntervals, DEPPromotedIntervals;
  bool DEPDisabled = false;
  std::shared_mutex IntervalsLock;
  int Invalidations = 0, Epoch = 0;
};
''' + function(tracker, "static bool ProtHasExec(") + "\n" + function(tracker, "static bool ProtIsReadable(") + "\n" + function(tracker, "static bool ProtIsWritable(") + "\n" + function(tracker, "void InvalidationTracker::HandleMemoryProtectionNotification(") + r'''
bool AcceptDiskHit(std::vector<uint64_t> pages) {
  struct { std::vector<uint64_t> GuestPages; } hit {pages};
  auto* Hit = &hit;
  struct { bool IsValidating() { return false; } } DiskCache;
  auto IsVolatileCode = [](uint64_t page) { return page == 0x1000; };
''' + disk_guard + r'''
    return true;
  }
  return false;
}
struct TestBlock { uint64_t Size; uint64_t NumInstructions; bool ForceFullSMCDetection; };
__attribute__((noinline)) uint64_t Sample(uint64_t GuestRIP, uint64_t size) {
  const TestBlock Block {size, 0, false};
''' + sample + r'''
  return Sample;
}
struct TestContext {
  bool IsVolatileCode(uint64_t page) const { return page == 0x1000; }
};
bool DecodeNext(uint64_t address, uint64_t& CurrentCodePage, TestBlock& block, bool StopAtVolatileCode = true) {
  TestContext context;
  auto* CTX = &context;
  auto* BlockIt = &block;
  bool EntryBlock = true;
  uint64_t OpMinPage = address & ~uint64_t{4095};
  uint64_t OpMaxPage = (address + 15) & ~uint64_t{4095};
  struct { std::set<uint64_t> CodePages; } BlockInfo;
  for (;;) {
''' + guard + r'''
    return true;
  }
  return false;
}
''' + rip_struct + r'''
constexpr size_t RipTableSize = 8192;
''' + function(module, "size_t HashSlot(") + "\n" + function(module, "void AddRip(") + "\n" + exc + r'''
fextl::string Describe(uint64_t) { return "site"; }
fextl::string ExceptionOutput() {
  using Entry = std::remove_reference_t<decltype(ExcTable[0])>;
  fextl::vector<Entry> Exc;
  for (const auto& e : ExcTable) if (e.Count) Exc.push_back(e);
  fextl::string Out;
  for (size_t i = 0; i < Exc.size(); ++i) {
''' + exc_format + r'''
  }
  return Out;
}
int main(int argc, char** argv) {
  for (int sig : {SIGSEGV, SIGBUS, SIGABRT}) std::signal(sig, [](int value) { _exit(128 + value); });
  struct rlimit limit {0, 0};
  setrlimit(RLIMIT_CORE, &limit);
  assert(argc == 2);
  Page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  Image = static_cast<uint8_t*>(mmap(nullptr, 2 * Page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
  assert(Image != MAP_FAILED);
  const std::string mode = argv[1];
  if (mode == "sample") {
    auto* rip = Image + Page - 2;
    rip[0] = 0xff; rip[1] = 0xd0;
    assert(mprotect(Image + Page, Page, PROT_NONE) == 0);
    assert(Sample(reinterpret_cast<uintptr_t>(rip), 2) == 0xd0ff);
    assert(Sample(reinterpret_cast<uintptr_t>(Image + Page), 0) == 0);
  } else if (mode == "boundary") {
    uint64_t current = 0;
    TestBlock block {0, 0, false};
    assert(DecodeNext(0xffe, current, block));
    assert(block.ForceFullSMCDetection);
    ++block.NumInstructions;
    assert(!DecodeNext(0x1003, current, block));
    current = 0x1000;
    assert(!DecodeNext(0x1004, current, block));
    TestBlock ordinary {0, 0, false};
    current = 0x2000;
    assert(DecodeNext(0x2100, current, ordinary));
    assert(!ordinary.ForceFullSMCDetection);
    TestBlock vol {0, 0, false};
    assert(DecodeNext(0x1100, current, vol, false));
    assert(!vol.ForceFullSMCDetection);
  } else if (mode == "module") {
    assert(mprotect(Image, Page, PROT_NONE) == 0);
    const auto* module = FindModule(reinterpret_cast<uintptr_t>(Image + Page + 8));
    assert(module && module->Base == reinterpret_cast<uintptr_t>(Image));
    assert(module->End == reinterpret_cast<uintptr_t>(Image + 2 * Page));
    assert(module->Name == "image");
  } else if (mode == "exceptions") {
    OnGuestException(0xc0000005, 0x1234, true);
    OnGuestException(0x40000005, 0x1234, false);
    OnGuestException(0xc0000005, 0x1234, true);
    OnGuestException(0x80000003, 0x1234, true);
    size_t entries = 0;
    for (const auto& e : ExcTable) entries += e.Count != 0;
    assert(entries == 3 && ExcCount == 4 && ExcSlow == 1);
    const auto output = ExceptionOutput();
    assert(output.find("c0000005@site:2") != std::string::npos);
    assert(output.find("40000005@site:1") != std::string::npos);
    assert(output.find("80000003@site:1") != std::string::npos);
  } else if (mode == "reuse_policy") {
    using Decoder = FEXCore::Frontend::Decoder;
    Decoder::DecodedBlockInformation info {{{reinterpret_cast<uintptr_t>(Image), 2, false, 0, Decoder::DecodedBlockStatus::SUCCESS}}};
    const auto ordinary = HashDecodedBlocks(&info);
    info.Blocks[0].ForceFullSMCDetection = true;
    assert(HashDecodedBlocks(&info) != ordinary);
    info.Blocks[0].Size = 0;
    info.Blocks[0].BlockStatus = Decoder::DecodedBlockStatus::INVALID_INST;
    info.Blocks[0].FailExaminedSize = 2;
    const auto illegal = HashDecodedBlocks(&info);
    info.Blocks[0].BlockStatus = Decoder::DecodedBlockStatus::NOEXEC_INST;
    assert(HashDecodedBlocks(&info) != illegal);
  } else if (mode == "permissions") {
    InvalidationTracker tracker;
    tracker.HandleMemoryProtectionNotification(0x1000, 4096, PAGE_READWRITE);
    assert(tracker.Epoch == 1 && tracker.Invalidations == 1);
    tracker.HandleMemoryProtectionNotification(0x1000, 4096, PAGE_EXECUTE_READWRITE);
    assert(tracker.Invalidations == 2);
  } else if (mode == "failed_decode") {
    using Decoder = FEXCore::Frontend::Decoder;
    Decoder::DecodedBlocks block {0, 0, true, 2, Decoder::DecodedBlockStatus::INVALID_INST};
    assert(ValidationLength(block) == 2);
    block.BlockStatus = Decoder::DecodedBlockStatus::UNIMPLEMENTED_INST;
    assert(ValidationLength(block) == 2);
    block.BlockStatus = Decoder::DecodedBlockStatus::NOEXEC_INST;
    assert(ValidationLength(block) == 0);
    block.BlockStatus = Decoder::DecodedBlockStatus::PARTIAL_DECODE_INST;
    assert(ValidationLength(block) == 0);
  } else if (mode == "disk_cache") {
    assert(AcceptDiskHit({0x2000}));
    assert(!AcceptDiskHit({0x0000, 0x1000}));
  } else if (mode == "failed_entry_hash") {
    using Decoder = FEXCore::Frontend::Decoder;
    Image[0] = 0x0f; Image[1] = 0x04;
    Decoder::DecodedBlockInformation info {{{reinterpret_cast<uintptr_t>(Image), 0, true, 2, Decoder::DecodedBlockStatus::INVALID_INST}}};
    const auto before = HashDecodedBlocks(&info);
    Image[1] = 0x0a;
    assert(HashDecodedBlocks(&info) != before);
  } else if (mode == "failed_tail") {
    using Decoder = FEXCore::Frontend::Decoder;
    Decoder::DecodedBlocks block {0, 1, true, 2, Decoder::DecodedBlockStatus::INVALID_INST, 2};
    assert(ValidationLength(block) == 0);
  } else return 2;
  munmap(Image, 2 * Page);
}
'''


class RegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="fex-volatile-tests-")
        folder = Path(cls.temp.name)
        source = folder / "regressions.cpp"
        source.write_text(harness(TREE))
        cls.binary = folder / "regressions"
        compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            raise RuntimeError("A host C++20 compiler is required")
        subprocess.run([compiler, "-std=c++20", "-O2", "-pthread", "-I", str(TREE / "External/fmt/include"),
                        str(source), "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check_case(self, mode):
        result = subprocess.run([str(self.binary), mode], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr or f"{mode} failed with status {result.returncode}")

    def test_diagnostic_sample_respects_decoded_size(self):
        self.check_case("sample")

    def test_cross_page_entry_checks_smc_and_stops_at_volatile_code(self):
        self.check_case("boundary")

    def test_module_lookup_does_not_read_inaccessible_headers(self):
        self.check_case("module")

    def test_exception_keys_and_output_preserve_complete_status(self):
        self.check_case("exceptions")

    def test_reuse_hash_includes_smc_and_exception_policy(self):
        self.check_case("reuse_policy")

    def test_volatile_protection_changes_invalidate_decoder_permissions(self):
        self.check_case("permissions")

    def test_failed_decode_validates_examined_bytes_but_not_inaccessible_bytes(self):
        self.check_case("failed_decode")

    def test_disk_cache_cannot_restore_code_for_volatile_pages(self):
        self.check_case("disk_cache")

    def test_reuse_hash_tracks_examined_failed_entry_bytes(self):
        self.check_case("failed_entry_hash")

    def test_invalid_tail_has_no_crc_dependency_outside_volatile_cache_ranges(self):
        self.check_case("failed_tail")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fex", type=Path)
    args = parser.parse_args()
    TREE = args.fex.resolve()
    unittest.main(argv=[__file__], verbosity=2)
