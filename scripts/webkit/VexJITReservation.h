/* Private WebKit platform adapter. The public JIT ABI remains Vex's existing
 * tagged, structured arena calls; no Linux RWX request is reinterpreted. */
#pragma once

#include <unos/vex_jit_abi.h>
#include <unos/vex_memory_abi.h>
#include <vex_syscall.h>
#include <wtf/Assertions.h>
#include <wtf/DataLog.h>
#include <wtf/PageBlock.h>

namespace JSC {

class VexJITReservation {
public:
    explicit operator bool() const { return m_arena.arena_id; }
    void* base() const { return m_base; }
    void* end() const { return static_cast<uint8_t*>(m_base) + m_size; }
    size_t size() const { return m_size; }
    void* writableBase() const { return static_cast<uint8_t*>(m_arena.rw_base) + WTF::pageSize(); }

    static VexJITReservation tryReserveWithGuardPages(size_t size, WTF::OSAllocator::Usage,
        void* hint, bool, bool, bool, bool)
    {
        VexJITReservation result;
        const size_t guard = WTF::pageSize();
        if (!size || size > SIZE_MAX - 2 * guard || (size % guard))
            return result;
        // A non-null JSC reservation hint is an exact RX address contract.
        if (hint && (reinterpret_cast<uintptr_t>(hint) < guard
                || reinterpret_cast<uintptr_t>(hint) % guard))
            return result;
        vex_jit_arena_create_req_t request { };
        request.preferred_rx_addr = hint ? static_cast<uint8_t*>(hint) - guard : nullptr;
        request.size = size + 2 * guard;
        request.quota_bytes = request.size; // Cap committed pages, not reserved VA.
        if (vex_syscall1(VEX_SYS_JIT_ARENA_CREATE, reinterpret_cast<uintptr_t>(&request)))
            return result;
        result.m_arena = request;
        result.m_base = static_cast<uint8_t*>(request.rx_base) + guard;
        result.m_size = size;
        for (void* alias : { request.rw_base, request.rx_base }) {
            for (size_t offset : { size_t(0), size + guard }) {
                vex_memory_req_t protection { static_cast<uint8_t*>(alias) + offset, guard, 0 };
                if (vex_syscall1(VEX_SYS_MPROTECT, reinterpret_cast<uintptr_t>(&protection))) {
                    result.deallocate();
                    return { };
                }
            }
        }
        WTF::dataLogLn("VexJIT: dual-alias arena id=", request.arena_id,
            " reserved=", size, " RW/NX=", WTF::RawPointer(result.writableBase()),
            " RX/RO=", WTF::RawPointer(result.base()));
        return result;
    }

    static VexJITReservation tryReserve(size_t size, WTF::OSAllocator::Usage usage,
        void* hint, bool writable, bool executable, bool commit, bool cage)
    {
        // Vex has no JSC JIT cage contract. Never pretend that it does.
        if (cage)
            return { };
        return tryReserveWithGuardPages(size, usage, hint, writable, executable, commit, cage);
    }

    void commit(void* address, size_t bytes)
    {
        checkRange(address, bytes);
        RELEASE_ASSERT(bytes <= m_size - m_committed);
        // The existing sparse backing materializes on write/fetch. No eager
        // allocation or protection change is necessary for logical commit.
        m_committed += bytes;
    }

    void decommit(void* address, size_t bytes)
    {
        checkRange(address, bytes);
        RELEASE_ASSERT(bytes <= m_committed);
        vex_memory_req_t request { address, bytes, 4 }; // MADV_DONTNEED
        RELEASE_ASSERT(!vex_syscall1(VEX_SYS_MADVISE, reinterpret_cast<uintptr_t>(&request)));
        m_committed -= bytes;
    }

    void deallocate()
    {
        if (!m_arena.arena_id)
            return;
        vex_jit_arena_destroy_req_t request { m_arena.rx_base, m_arena.arena_id, 0 };
        RELEASE_ASSERT(!vex_syscall1(VEX_SYS_JIT_ARENA_DESTROY, reinterpret_cast<uintptr_t>(&request)));
        *this = { };
    }

private:
    void checkRange(void* address, size_t bytes) const
    {
        const uintptr_t offset = reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(m_base);
        RELEASE_ASSERT(*this && !(offset % WTF::pageSize()) && !(bytes % WTF::pageSize()));
        RELEASE_ASSERT(offset <= m_size && bytes <= m_size - offset);
    }
    vex_jit_arena_create_req_t m_arena { };
    void* m_base { nullptr };
    size_t m_size { 0 };
    size_t m_committed { 0 };
};

} // namespace JSC
