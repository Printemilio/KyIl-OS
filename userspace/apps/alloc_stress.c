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
    uint8_t seeds[ALLOC_SLOTS];
    phase_stats_t stats;

    for (size_t i = 0; i < ALLOC_SLOTS; ++i) {
        slots[i] = 0;
        sizes[i] = 0u;
        seeds[i] = 0u;
    }

    stats.ops = 0u;
    stats.bytes = 0u;
    stats.start_ms = now_ms();

    for (size_t round = 0; round < ALLOC_ROUNDS; ++round) {
        for (size_t i = 0; i < ALLOC_SLOTS; ++i) {
            size_t size = ((round * 131u) + (i * 67u)) % 3000u + 1u;
            uint8_t* ptr = (uint8_t*)malloc(size);
            if (ptr == 0) {
                log_text("mrac_bench: alloc churn malloc failed\n");
                return 14;
            }
            fill_pattern(ptr, size, i, round + 1u);
            slots[i] = ptr;
            sizes[i] = size;
            seeds[i] = (uint8_t)(round + i);
            ++stats.ops;
            stats.bytes += size;
        }

        for (size_t i = 0; i < ALLOC_SLOTS; ++i) {
            size_t new_size = ((round * 193u) + (i * 29u)) % 3000u + 1u;
            size_t keep = sizes[i] < new_size ? sizes[i] : new_size;
            size_t verify = keep < VERIFY_LIMIT ? keep : VERIFY_LIMIT;
            uint8_t* resized = (uint8_t*)realloc(slots[i], new_size);
            if (resized == 0) {
                log_text("mrac_bench: alloc churn realloc failed\n");
                return 15;
            }
            if (verify != 0u && !check_pattern(resized, verify, i, round + 1u)) {
                log_text("mrac_bench: alloc churn verify failed\n");
                return 16;
            }
            fill_pattern(resized, new_size, i, round + 1u);
            slots[i] = resized;
            sizes[i] = new_size;
            ++stats.ops;
            stats.bytes += new_size;
        }
    }

    for (size_t i = 0; i < ALLOC_SLOTS; ++i) {
        free(slots[i]);
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
