#include <stddef.h>
#include <stdint.h>

#include <unos/vex_legacy_compat.h>
#include <unos/vex_abi.h>

#include "vex_runtime.h"
#include "vex_syscall.h"

extern "C" {

void* stderr = nullptr;
char* program_invocation_name = (char*)"webkit";
char* tzname[2] = { (char*)"UTC", (char*)"UTC" };

void arc4random_buf(void* buffer, size_t size)
{
    if (buffer == nullptr || size == 0)
        return;
    vex_getrandom_req_t req;
    req.buffer = buffer;
    req.length = size;
    req.flags = 0;
    (void)vex_syscall1(VEX_SYS_GETRANDOM, (uint64_t)&req);
}


void* mi_calloc(size_t count, size_t size)
{
    return calloc(count, size);
}

void* mi_malloc(size_t size)
{
    return malloc(size);
}

void* mi_realloc(void* ptr, size_t size)
{
    return realloc(ptr, size);
}

size_t mi_good_size(size_t size)
{
    return malloc_good_size(size);
}

void mi_free(void* ptr)
{
    free(ptr);
}

void* mi_malloc_aligned(size_t size, size_t alignment)
{
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0)
        return nullptr;
    return ptr;
}

void* mi_zalloc(size_t size)
{
    return calloc(1, size);
}

void* mi_zalloc_aligned(size_t size, size_t alignment)
{
    void* ptr = mi_malloc_aligned(size, alignment);
    if (ptr != nullptr)
        __builtin_memset(ptr, 0, size);
    return ptr;
}

void* mi_realloc_aligned(void* ptr, size_t size, size_t alignment)
{
    (void)alignment;
    return realloc(ptr, size);
}

using pthread_t = unsigned long;

pthread_t pthread_self(void)
{
    return 1;
}

int pthread_equal(pthread_t left, pthread_t right)
{
    return left == right;
}

int pthread_once(void* once_control, void (*init_routine)(void))
{
    uint32_t* state = static_cast<uint32_t*>(once_control);
    if (*state == 0) {
        *state = 1;
        init_routine();
    }
    return 0;
}

int pthread_mutex_lock(void*)
{
    return 0;
}

int pthread_mutex_unlock(void*)
{
    return 0;
}

int pthread_mutex_init(void*, const void*)
{
    return 0;
}

int pthread_mutex_destroy(void*)
{
    return 0;
}

int pthread_cond_init(void*, const void*)
{
    return 0;
}

int pthread_cond_destroy(void*)
{
    return 0;
}

int pthread_cond_signal(void*)
{
    return 0;
}

int pthread_cond_broadcast(void*)
{
    return 0;
}

int pthread_cond_wait(void*, void*)
{
    return 0;
}

int pthread_getname_np(pthread_t, char* name, size_t size)
{
    if (name != nullptr && size != 0)
        name[0] = '\0';
    return 0;
}

int sched_yield(void)
{
    return 0;
}

int gettid(void)
{
    return 1;
}

int fputs(const char* text, void*)
{
    if (text == nullptr)
        return -1;
    return write(STDERR_FILENO, text, __builtin_strlen(text)) < 0 ? -1 : 0;
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, void*)
{
    size_t total = size * nmemb;
    if (total == 0)
        return 0;
    return write(STDERR_FILENO, ptr, total) < 0 ? 0 : nmemb;
}

int ferror(void*)
{
    return 0;
}

struct tm* localtime_r(const time_t*, struct tm* result)
{
    if (result != nullptr)
        __builtin_memset(result, 0, sizeof(*result));
    return result;
}

struct tm* gmtime_r(const time_t*, struct tm* result)
{
    if (result != nullptr)
        __builtin_memset(result, 0, sizeof(*result));
    return result;
}

time_t mktime(struct tm*)
{
    return 0;
}

time_t timegm(struct tm*)
{
    return 0;
}

int isinf(double value)
{
    return __builtin_isinf(value);
}

int isnan(double value)
{
    return __builtin_isnan(value);
}

float __extendhfsf2(uint16_t value)
{
    return static_cast<float>(value);
}

int pthread_rwlock_destroy(void*)
{
    return 0;
}

int pthread_rwlock_init(void*, void const*)
{
    return 0;
}

int pthread_rwlock_rdlock(void*)
{
    return 0;
}

int pthread_rwlock_wrlock(void*)
{
    return 0;
}

int pthread_rwlock_unlock(void*)
{
    return 0;
}

double floor(double x)
{
    return __builtin_floor(x);
}

float floorf(float x)
{
    return __builtin_floorf(x);
}

int isfinite(double x)
{
    return __builtin_isfinite(x);
}

void* memchr(void const* s, int c, size_t n)
{
    unsigned char const* p = (unsigned char const*)s;
    while (n--) {
        if (*p == (unsigned char)c)
            return (void*)p;
        ++p;
    }
    return nullptr;
}

}

namespace std {
struct nothrow_t {
    explicit nothrow_t() = default;
};

extern const nothrow_t nothrow;
const nothrow_t nothrow {};
}

void* operator new(size_t size, std::nothrow_t const&) noexcept
{
    return malloc(size);
}

void* operator new[](size_t size, std::nothrow_t const&) noexcept
{
    return malloc(size);
}

void operator delete(void* ptr, std::nothrow_t const&) noexcept
{
    free(ptr);
}

void operator delete[](void* ptr, std::nothrow_t const&) noexcept
{
    free(ptr);
}
