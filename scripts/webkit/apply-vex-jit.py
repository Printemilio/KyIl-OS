#!/usr/bin/env python3
"""Apply the audited Vex dual-alias adapter to official WPE 2.53.3 sources.

Every replacement is checked; a different upstream source must be audited,
not patched heuristically. Original sources remain beside the patched file.
"""
from pathlib import Path
import shutil
import sys

root = Path(sys.argv[1])
path = root / "Source/JavaScriptCore/jit/ExecutableAllocator.cpp"
original = path.with_suffix(".cpp.vex-original")
if not original.exists():
    shutil.copyfile(path, original)
text = original.read_text()

def replace(old, new):
    global text
    if text.count(old) != 1:
        raise SystemExit(f"Unexpected WPE allocator source: {old[:100]!r}")
    text = text.replace(old, new)

replace('#include <wtf/WorkQueue.h>', '#include <wtf/WorkQueue.h>\n#if ENABLE(VEX_JIT)\n#include "VexJITReservation.h"\n#endif')
replace('struct JITReservation {\n    PageReservation pageReservation;', '''#if ENABLE(VEX_JIT)
using JITPageReservation = VexJITReservation;

static void genericVexWriteToJITRegion(off_t offset, const void* source, size_t size)
{
    const uintptr_t poolSize = reinterpret_cast<uintptr_t>(g_jscConfig.endExecutableMemory)
        - reinterpret_cast<uintptr_t>(g_jscConfig.startExecutableMemory);
    RELEASE_ASSERT(offset >= 0 && static_cast<uintptr_t>(offset) <= poolSize
        && size <= poolSize - static_cast<uintptr_t>(offset));
    // Keep JSC's atomic patching semantics for instruction/data slots. The RX
    // address remains the linker/call target; only the physical write is aliased.
    memcpyAtomicIfPossible(reinterpret_cast<void*>(g_jscConfig.startOfFixedWritableMemoryPool
        + static_cast<uintptr_t>(offset)), source, size);
}
#else
using JITPageReservation = PageReservation;
#endif

struct JITReservation {
    JITPageReservation pageReservation;''')
# Same allocator/commit/free algorithm, platform-specific reservation only.
text = text.replace('return PageReservation::tryReserve', 'return JITPageReservation::tryReserve')
replace('    PageReservation m_reservation;', '    JITPageReservation m_reservation;')
replace('''#if ENABLE(SEPARATED_WX_HEAP)
        if (!g_jscConfig.useFastJITPermissions) {''', '''#if ENABLE(VEX_JIT)
        RELEASE_ASSERT(!g_jscConfig.useFastJITPermissions);
        g_jscConfig.startOfFixedWritableMemoryPool = reinterpret_cast<uintptr_t>(reservation.pageReservation.writableBase());
        g_jscConfig.jitWriteSeparateHeaps = reinterpret_cast<JITWriteSeparateHeapsFunction>(&genericVexWriteToJITRegion);
#elif ENABLE(SEPARATED_WX_HEAP)
        if (!g_jscConfig.useFastJITPermissions) {''')
if path.read_text() != text:
    path.write_text(text)

def patch_original(relative, transform):
    file = root / relative
    backup = file.with_name(file.name + ".vex-original")
    if not backup.exists():
        shutil.copyfile(file, backup)
    patched = transform(backup.read_text())
    if file.read_text() != patched:
        file.write_text(patched)

def patch_x86(source):
    old = "#if ENABLE(MPROTECT_RX_TO_RWX)"
    if source.count(old) != 10:
        raise SystemExit("Unexpected x86 pointer/int patch implementation")
    # Includes link-time stores, inline-cache rewrites, jump replacement,
    # invalidation/HLT, and nop fills. Relative targets are computed from RX;
    # only the final store is translated by the shared write hook.
    return source.replace(old, "#if ENABLE(MPROTECT_RX_TO_RWX) || ENABLE(VEX_JIT)")

patch_original("Source/JavaScriptCore/assembler/X86Assembler.h", patch_x86)

def patch_copy(source):
    old = "    static_assert(!(*repatch).contains(RepatchingFlag::Flush));\n    if constexpr (is32Bit()) {"
    if source.count(old) != 1:
        raise SystemExit("Unexpected common machine-code copy implementation")
    return source.replace(old, '''    static_assert(!(*repatch).contains(RepatchingFlag::Flush));
#if ENABLE(VEX_JIT)
    // Linking and finalized-code patching share one write path. Temporary
    // assembler buffers still use normal copies (performJITMemcpy checks the
    // destination); executable-pool stores use the native RW alias.
    if constexpr ((*repatch).contains(RepatchingFlag::Memcpy)) {
        constexpr RepatchingInfo aliasRepatch = (*repatch).contains(RepatchingFlag::Atomic)
            ? jitMemcpyRepatchAtomic : jitMemcpyRepatch;
        return performJITMemcpy<aliasRepatch>(dst, src, n);
    }
#endif
    if constexpr (is32Bit()) {''')

patch_original("Source/JavaScriptCore/assembler/AssemblerCommon.h", patch_copy)

def copy_if_changed(source, destination):
    if not destination.exists() or source.read_bytes() != destination.read_bytes():
        shutil.copyfile(source, destination)

copy_if_changed(Path(__file__).with_name("VexJITReservation.h"), path.with_name("VexJITReservation.h"))
project = Path(__file__).resolve().parents[2]
include = root / "vex-include"
(include / "unos").mkdir(parents=True, exist_ok=True)
for name in ("vex_jit_abi.h", "vex_memory_abi.h", "syscall_wire.h"):
    copy_if_changed(project / "kernel/include/unos" / name, include / "unos" / name)
copy_if_changed(project / "userspace/libvex/include/vex_syscall.h", include / "vex_syscall.h")
print("WPE 2.53.3: Vex native dual-alias reservation + common atomic JIT write hook installed")
