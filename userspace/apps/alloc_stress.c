#include <stddef.h>
#include <stdint.h>

#include <unos/vex_abi.h>

#include "vex_runtime.h"
#include "vex_syscall.h"

#define KEY_COUNT 128u
#define HISTORY_DEPTH 16u
#define MAX_BUFFER_SIZE 65536u
#define HOT_ROUNDS 1024u
#define VERSION_ROUNDS 128u
#define MIXED_OPS 100000u
#define ALLOC_ROUNDS 64u
#define ALLOC_SLOTS 192u
#define VERIFY_LIMIT 128u

typedef struct phase_stats {
    uint64_t ops;
    uint64_t bytes;
    uint64_t start_ms;
} phase_stats_t;

static uint32_t g_versions[KEY_COUNT][HISTORY_DEPTH];
static uint8_t g_version_counts[KEY_COUNT];
static uint8_t g_key_present[KEY_COUNT];
static size_t g_key_sizes[KEY_COUNT];
static uint8_t g_input[MAX_BUFFER_SIZE];
static uint8_t g_output[MAX_BUFFER_SIZE];

static void log_text(const char* text)
{
    (void)vex_syscall1(VEX_SYS_DEBUG_LOG, (uint64_t)text);
}

static size_t append_text(char* buffer, size_t pos, size_t capacity, const char* text)
{
    while (*text != '\0' && pos + 1u < capacity) {
        buffer[pos++] = *text++;
    }
    buffer[pos] = '\0';
    return pos;
}

static size_t append_u64(char* buffer, size_t pos, size_t capacity, uint64_t value)
{
    char digits[32];
    size_t count = 0u;

    if (value == 0u) {
        if (pos + 1u < capacity) {
            buffer[pos++] = '0';
            buffer[pos] = '\0';
        }
        return pos;
    }

    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count != 0u && pos + 1u < capacity) {
        buffer[pos++] = digits[--count];
    }
    buffer[pos] = '\0';
    return pos;
}

static void log_phase(const char* name, uint64_t ops, uint64_t bytes, uint64_t elapsed_ms)
{
    char line[192];
    size_t pos = 0u;
    uint64_t ops_per_s = elapsed_ms == 0u ? ops * 1000u : (ops * 1000u) / elapsed_ms;
    uint64_t kb_per_s = elapsed_ms == 0u ? (bytes * 1000u) / 1024u : ((bytes * 1000u) / elapsed_ms) / 1024u;

    pos = append_text(line, pos, sizeof(line), "mrac_bench: ");
    pos = append_text(line, pos, sizeof(line), name);
    pos = append_text(line, pos, sizeof(line), " ms=");
    pos = append_u64(line, pos, sizeof(line), elapsed_ms);
    pos = append_text(line, pos, sizeof(line), " ops=");
    pos = append_u64(line, pos, sizeof(line), ops);
    pos = append_text(line, pos, sizeof(line), " ops_s=");
    pos = append_u64(line, pos, sizeof(line), ops_per_s);
    pos = append_text(line, pos, sizeof(line), " kb_s=");
    pos = append_u64(line, pos, sizeof(line), kb_per_s);
    pos = append_text(line, pos, sizeof(line), "\n");
    (void)pos;
    log_text(line);
}

static void log_alloc_detail(const char* label, size_t slot, uint32_t version, uintptr_t addr, size_t size)
{
    char line[224];
    size_t pos = 0u;

    pos = append_text(line, pos, sizeof(line), "mrac_bench: ");
    pos = append_text(line, pos, sizeof(line), label);
    pos = append_text(line, pos, sizeof(line), " slot=");
    pos = append_u64(line, pos, sizeof(line), slot);
    pos = append_text(line, pos, sizeof(line), " version=");
    pos = append_u64(line, pos, sizeof(line), version);
    pos = append_text(line, pos, sizeof(line), " addr=");
    pos = append_u64(line, pos, sizeof(line), (uint64_t)addr);
    pos = append_text(line, pos, sizeof(line), " size=");
    pos = append_u64(line, pos, sizeof(line), size);
    pos = append_text(line, pos, sizeof(line), "\n");
    (void)pos;
    log_text(line);
}

static void log_alloc_mismatch(size_t slot, uint32_t version, uintptr_t addr, size_t size, size_t offset, uint8_t expected, uint8_t actual)
{
    char line[256];
    size_t pos = 0u;

    pos = append_text(line, pos, sizeof(line), "mrac_bench: alloc mismatch slot=");
    pos = append_u64(line, pos, sizeof(line), slot);
    pos = append_text(line, pos, sizeof(line), " version=");
    pos = append_u64(line, pos, sizeof(line), version);
    pos = append_text(line, pos, sizeof(line), " addr=");
    pos = append_u64(line, pos, sizeof(line), (uint64_t)addr);
    pos = append_text(line, pos, sizeof(line), " size=");
    pos = append_u64(line, pos, sizeof(line), size);
    pos = append_text(line, pos, sizeof(line), " off=");
    pos = append_u64(line, pos, sizeof(line), offset);
    pos = append_text(line, pos, sizeof(line), " expected=");
    pos = append_u64(line, pos, sizeof(line), expected);
    pos = append_text(line, pos, sizeof(line), " actual=");
    pos = append_u64(line, pos, sizeof(line), actual);
    pos = append_text(line, pos, sizeof(line), "\n");
    (void)pos;
    log_text(line);
}

static uint64_t now_ms(void)
{
    vex_bulk_result_t result;
    vex_bulk_req_t req;

    req.mask = VEX_BULK_TIME;
    req.result = &result;
    if ((int64_t)vex_syscall1(VEX_SYS_BULK, (uint64_t)&req) != 0) {
        return 0u;
    }
    return result.monotonic_ms;
}

static uint32_t next_random(uint32_t* state)
{
    *state = (*state * 1664525u) + 1013904223u;
    return *state;
}

static void fill_pattern(uint8_t* buffer, size_t size, uint64_t key, uint32_t version)
{
    uint8_t seed = (uint8_t)(key ^ version);
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = (uint8_t)(seed + (uint8_t)(i * 13u));
    }
}

static int check_pattern(const uint8_t* buffer, size_t size, uint64_t key, uint32_t version)
{
    uint8_t seed = (uint8_t)(key ^ version);
    for (size_t i = 0; i < size; ++i) {
        if (buffer[i] != (uint8_t)(seed + (uint8_t)(i * 13u))) {
            return 0;
        }
    }
    return 1;
}

static size_t first_pattern_mismatch(const uint8_t* buffer, size_t size, uint64_t key, uint32_t version, uint8_t* expected_out, uint8_t* actual_out)
{
    uint8_t seed = (uint8_t)(key ^ version);
    for (size_t i = 0; i < size; ++i) {
        uint8_t expected = (uint8_t)(seed + (uint8_t)(i * 13u));
        if (buffer[i] != expected) {
            if (expected_out != 0) {
                *expected_out = expected;
            }
            if (actual_out != 0) {
                *actual_out = buffer[i];
            }
            return i;
        }
    }
    return size;
}

static void record_version(size_t key_index, uint32_t version)
{
    uint8_t count = g_version_counts[key_index];

    if (count < HISTORY_DEPTH) {
        g_versions[key_index][count] = version;
        g_version_counts[key_index] = (uint8_t)(count + 1u);
        return;
    }

    for (size_t i = 1u; i < HISTORY_DEPTH; ++i) {
        g_versions[key_index][i - 1u] = g_versions[key_index][i];
    }
    g_versions[key_index][HISTORY_DEPTH - 1u] = version;
}

static int phase_put_hot(void)
{
    phase_stats_t stats;

    stats.ops = 0u;
    stats.bytes = 0u;
    stats.start_ms = now_ms();

    for (size_t round = 0; round < HOT_ROUNDS; ++round) {
        for (size_t key_index = 0; key_index < KEY_COUNT; ++key_index) {
            uint64_t key = 0xC0DE0000ull + key_index;
            size_t size = ((round * 257u) + (key_index * 97u)) % 8192u + 64u;
            uint32_t version = 0u;

            fill_pattern(g_input, size, key, (uint32_t)round + 1u);
            if (vex_mrac_put(key, g_input, size, &version) != 0) {
                log_text("mrac_bench: put_hot failed\n");
                return 1;
            }

            record_version(key_index, version);
            g_key_present[key_index] = 1u;
            g_key_sizes[key_index] = size;
            ++stats.ops;
            stats.bytes += size;
        }
    }

    log_phase("put_hot", stats.ops, stats.bytes, now_ms() - stats.start_ms);
    return 0;
}

static int phase_get_hot(void)
{
    phase_stats_t stats;

    stats.ops = 0u;
    stats.bytes = 0u;
    stats.start_ms = now_ms();

    for (size_t round = 0; round < HOT_ROUNDS; ++round) {
        for (size_t key_index = 0; key_index < KEY_COUNT; ++key_index) {
            uint64_t key = 0xC0DE0000ull + key_index;
            uint8_t count = g_version_counts[key_index];
            size_t out_size = 0u;
            uint32_t resolved = 0u;
            uint32_t version;
            size_t size;

            if (count == 0u) {
                log_text("mrac_bench: get_hot missing version\n");
                return 2;
            }

            version = g_versions[key_index][count - 1u];
            size = g_key_sizes[key_index];
            if (vex_mrac_get(key, version, g_output, size, &out_size, &resolved) != 0) {
                log_text("mrac_bench: get_hot failed\n");
                return 3;
            }
            if (out_size != size || resolved != version || !check_pattern(g_output, size, key, resolved)) {
                log_text("mrac_bench: get_hot verify failed\n");
                return 4;
            }

            ++stats.ops;
            stats.bytes += size;
        }
    }

    log_phase("get_hot", stats.ops, stats.bytes, now_ms() - stats.start_ms);
    return 0;
}

static int phase_versions(void)
{
    phase_stats_t stats;

    stats.ops = 0u;
    stats.bytes = 0u;
    stats.start_ms = now_ms();

    for (size_t round = 0; round < VERSION_ROUNDS; ++round) {
        for (size_t key_index = 0; key_index < KEY_COUNT; ++key_index) {
            uint64_t key = 0xD00D0000ull + key_index;
            size_t size = ((round * 541u) + (key_index * 151u)) % 16384u + 128u;
            uint32_t version = 0u;

            fill_pattern(g_input, size, key, (uint32_t)round + 11u);
            if (vex_mrac_put(key, g_input, size, &version) != 0) {
                log_text("mrac_bench: versions put failed\n");
                return 5;
            }
            record_version(key_index, version);
            g_key_present[key_index] = 1u;
            g_key_sizes[key_index] = size;
            ++stats.ops;
            stats.bytes += size;
        }
    }

    for (size_t key_index = 0; key_index < KEY_COUNT; ++key_index) {
        uint64_t key = 0xD00D0000ull + key_index;
        size_t version_count = vex_mrac_version_count(key);
        uint32_t version_out = 0u;
        size_t size_out = 0u;
        uint8_t is_current = 0u;
        uint8_t found_current = 0u;

        if (version_count == 0u) {
            log_text("mrac_bench: version_count zero\n");
            return 6;
        }
        for (size_t index = 0; index < version_count; ++index) {
            if (vex_mrac_version_info(key, index, &version_out, &size_out, &is_current) != 0) {
                log_text("mrac_bench: version_info failed\n");
                return 7;
            }
            ++stats.ops;
            if (is_current != 0u) {
                found_current = 1u;
            }
        }

        if (found_current == 0u) {
            log_text("mrac_bench: version_info current missing\n");
            return 8;
        }
    }

    log_phase("versions", stats.ops, stats.bytes, now_ms() - stats.start_ms);
    return 0;
}

static int phase_mixed(void)
{
    phase_stats_t stats;
    uint32_t rng = 0x1234ABCDu;

    stats.ops = 0u;
    stats.bytes = 0u;
    stats.start_ms = now_ms();

    for (size_t op_index = 0; op_index < MIXED_OPS; ++op_index) {
        size_t key_index = next_random(&rng) % KEY_COUNT;
        uint64_t key = 0xBEEF0000ull + key_index;
        uint32_t op = next_random(&rng) % 5u;

        if (op == 0u || !g_key_present[key_index]) {
            size_t size = (next_random(&rng) % MAX_BUFFER_SIZE) + 1u;
            uint32_t version = 0u;

            fill_pattern(g_input, size, key, rng);
            if (vex_mrac_put(key, g_input, size, &version) != 0) {
                log_text("mrac_bench: mixed put failed\n");
                return 9;
            }
            record_version(key_index, version);
            g_key_present[key_index] = 1u;
            g_key_sizes[key_index] = size;
            stats.bytes += size;
        } else if (op == 1u || op == 2u) {
            size_t out_size = 0u;
            uint32_t resolved = 0u;
            uint8_t count = g_version_counts[key_index];
            uint32_t version = g_versions[key_index][(count == 0u ? 0u : (next_random(&rng) % count))];
            size_t size = g_key_sizes[key_index];

            if (vex_mrac_get(key, version, g_output, size, &out_size, &resolved) != 0) {
                log_text("mrac_bench: mixed get failed\n");
                return 10;
            }
            if (out_size != size || resolved != version || !check_pattern(g_output, size, key, resolved)) {
                log_text("mrac_bench: mixed get verify failed\n");
                return 11;
            }
            stats.bytes += size;
        } else if (op == 3u) {
            size_t version_count = vex_mrac_version_count(key);
            if (version_count != 0u) {
                vex_mrac_version_info_t info;
                if (vex_mrac_version_info(key, next_random(&rng) % version_count, &info.version, &info.size, &info.is_current) != 0) {
                    log_text("mrac_bench: mixed version_info failed\n");
                    return 12;
                }
            }
        } else {
            if (vex_mrac_remove(key, VEX_MRAC_VERSION_CURRENT, VEX_MRAC_REMOVE_ALL) != 0) {
                log_text("mrac_bench: mixed remove failed\n");
                return 13;
            }
            g_key_present[key_index] = 0u;
            g_key_sizes[key_index] = 0u;
            g_version_counts[key_index] = 0u;
        }

        ++stats.ops;
    }

    log_phase("mixed", stats.ops, stats.bytes, now_ms() - stats.start_ms);
    return 0;
}

static int phase_allocator_churn(void)
{
    void* slots[ALLOC_SLOTS];
    size_t sizes[ALLOC_SLOTS];
    uint32_t versions[ALLOC_SLOTS];
    phase_stats_t stats;

    for (size_t i = 0; i < ALLOC_SLOTS; ++i) {
        slots[i] = 0;
        sizes[i] = 0u;
        versions[i] = 0u;
    }

    stats.ops = 0u;
    stats.bytes = 0u;
    stats.start_ms = now_ms();

    for (size_t i = 0; i < ALLOC_SLOTS; ++i) {
        size_t size = (i * 67u) % 12288u + 4096u;
        vex_mmap_req_t req;
        uint8_t* ptr;

        req.addr = 0;
        req.length = size;
        req.prot = PROT_READ | PROT_WRITE;
        req.flags = MAP_PRIVATE | MAP_ANONYMOUS;
        req.fd = -1;
        req.offset = 0;
        ptr = (uint8_t*)(uintptr_t)vex_syscall1(VEX_SYS_MMAP, (uint64_t)&req);
        if (ptr == MAP_FAILED) {
            log_text("mrac_bench: alloc churn mmap failed\n");
            return 14;
        }
        versions[i] = 1u;
        fill_pattern(ptr, size, i, versions[i]);
        slots[i] = ptr;
        sizes[i] = size;
        ++stats.ops;
        stats.bytes += size;
    }

    for (size_t round = 0; round < ALLOC_ROUNDS; ++round) {
        for (size_t i = 0; i < ALLOC_SLOTS; ++i) {
            size_t new_size = ((round * 193u) + (i * 29u)) % 16384u + 4096u;
            size_t keep = sizes[i] < new_size ? sizes[i] : new_size;
            size_t verify = keep < VERIFY_LIMIT ? keep : VERIFY_LIMIT;
            vex_mmap_req_t req;
            vex_memory_req_t unmap;
            uint8_t* resized;

            if (verify != 0u && !check_pattern((const uint8_t*)slots[i], verify, i, versions[i])) {
                uint8_t expected = 0u;
                uint8_t actual = 0u;
                size_t mismatch = first_pattern_mismatch((const uint8_t*)slots[i], verify, i, versions[i], &expected, &actual);
                log_alloc_detail("alloc churn verify detail", i, versions[i], (uintptr_t)slots[i], sizes[i]);
                log_alloc_mismatch(i, versions[i], (uintptr_t)slots[i], sizes[i], mismatch, expected, actual);
                log_text("mrac_bench: alloc churn verify failed\n");
                return 16;
            }

            req.addr = 0;
            req.length = new_size;
            req.prot = PROT_READ | PROT_WRITE;
            req.flags = MAP_PRIVATE | MAP_ANONYMOUS;
            req.fd = -1;
            req.offset = 0;
            resized = (uint8_t*)(uintptr_t)vex_syscall1(VEX_SYS_MMAP, (uint64_t)&req);
            if (resized == MAP_FAILED) {
                log_text("mrac_bench: alloc churn remap mmap failed\n");
                return 15;
            }

            for (size_t other = 0; other < ALLOC_SLOTS; ++other) {
                uintptr_t a_start;
                uintptr_t a_end;
                uintptr_t b_start;
                uintptr_t b_end;

                if (slots[other] == 0 || sizes[other] == 0u) {
                    continue;
                }

                a_start = (uintptr_t)resized;
                a_end = a_start + new_size;
                b_start = (uintptr_t)slots[other];
                b_end = b_start + sizes[other];
                if (a_start < b_end && b_start < a_end) {
                    log_alloc_detail("alloc churn overlap detail", i, versions[i], (uintptr_t)resized, new_size);
                    log_text("mrac_bench: alloc churn mmap overlap\n");
                    return 20;
                }
            }

            for (size_t byte_index = 0; byte_index < keep; ++byte_index) {
                resized[byte_index] = ((uint8_t*)slots[i])[byte_index];
            }

            unmap.addr = slots[i];
            unmap.length = sizes[i];
            unmap.prot_or_advice = 0;
            if ((int64_t)vex_syscall1(VEX_SYS_MUNMAP, (uint64_t)&unmap) != 0) {
                log_text("mrac_bench: alloc churn munmap old failed\n");
                return 17;
            }

            slots[i] = resized;
            sizes[i] = new_size;
            ++versions[i];
            fill_pattern(resized, new_size, i, versions[i]);
            ++stats.ops;
            stats.bytes += new_size;
        }
    }

    for (size_t i = 0; i < ALLOC_SLOTS; ++i) {
        vex_memory_req_t unmap;
        size_t verify = sizes[i] < VERIFY_LIMIT ? sizes[i] : VERIFY_LIMIT;

        if (verify != 0u && !check_pattern((const uint8_t*)slots[i], verify, i, versions[i])) {
            uint8_t expected = 0u;
            uint8_t actual = 0u;
            size_t mismatch = first_pattern_mismatch((const uint8_t*)slots[i], verify, i, versions[i], &expected, &actual);
            log_alloc_detail("alloc churn final verify detail", i, versions[i], (uintptr_t)slots[i], sizes[i]);
            log_alloc_mismatch(i, versions[i], (uintptr_t)slots[i], sizes[i], mismatch, expected, actual);
            log_text("mrac_bench: alloc churn final verify failed\n");
            return 19;
        }

        unmap.addr = slots[i];
        unmap.length = sizes[i];
        unmap.prot_or_advice = 0;
        if ((int64_t)vex_syscall1(VEX_SYS_MUNMAP, (uint64_t)&unmap) != 0) {
            log_text("mrac_bench: alloc churn munmap final failed\n");
            return 18;
        }
        ++stats.ops;
    }

    log_phase("alloc_churn", stats.ops, stats.bytes, now_ms() - stats.start_ms);
    return 0;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    for (size_t i = 0; i < KEY_COUNT; ++i) {
        g_version_counts[i] = 0u;
        g_key_present[i] = 0u;
        g_key_sizes[i] = 0u;
    }

    log_text("mrac_bench: begin\n");

    if (phase_allocator_churn() != 0) {
        return 1;
    }
    if (phase_put_hot() != 0) {
        return 2;
    }
    if (phase_get_hot() != 0) {
        return 3;
    }
    if (phase_versions() != 0) {
        return 4;
    }
    if (phase_mixed() != 0) {
        return 5;
    }

    for (size_t key_index = 0; key_index < KEY_COUNT; ++key_index) {
        (void)vex_mrac_remove(0xC0DE0000ull + key_index, VEX_MRAC_VERSION_CURRENT, VEX_MRAC_REMOVE_ALL);
        (void)vex_mrac_remove(0xD00D0000ull + key_index, VEX_MRAC_VERSION_CURRENT, VEX_MRAC_REMOVE_ALL);
        (void)vex_mrac_remove(0xBEEF0000ull + key_index, VEX_MRAC_VERSION_CURRENT, VEX_MRAC_REMOVE_ALL);
    }

    log_text("mrac_bench: ok\n");
    return 0;
}
