#include <stdint.h>
#include <stddef.h>

extern "C" void* __dso_handle = nullptr;

extern "C" int __cxa_atexit(void (*destructor)(void*), void* arg, void* dso_handle) {
    (void)destructor;
    (void)arg;
    (void)dso_handle;
    return 0;
}

extern "C" void __cxa_pure_virtual(void) {
    for (;;) {
    }
}

extern "C" int __cxa_guard_acquire(uint64_t* guard) {
    return (*guard & 1u) == 0;
}

extern "C" void __cxa_guard_release(uint64_t* guard) {
    *guard |= 1u;
}

extern "C" void __cxa_guard_abort(uint64_t* guard) {
    (void)guard;
}

namespace __cxxabiv1 {

class __class_type_info {
public:
    virtual ~__class_type_info();
};

class __si_class_type_info : public __class_type_info {
public:
    virtual ~__si_class_type_info();
};

class __vmi_class_type_info : public __class_type_info {
public:
    virtual ~__vmi_class_type_info();
};

__class_type_info::~__class_type_info() = default;
__si_class_type_info::~__si_class_type_info() = default;
__vmi_class_type_info::~__vmi_class_type_info() = default;

}

extern "C" void* __dynamic_cast(void* sub, __cxxabiv1::__class_type_info const*, __cxxabiv1::__class_type_info const*, ptrdiff_t)
{
    return sub;
}
