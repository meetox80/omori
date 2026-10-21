#include <kernel.h>

#include <cstdint>
#include <new>

namespace {

constexpr int PTE_OS_OK = 0;
constexpr int PTE_OS_NO_RESOURCES = 1;

struct LazySema {
    int32_t id;
    int initial;
};

static_assert(sizeof(LazySema*) == sizeof(int), "pte semaphore handles must hold a pointer");

LazySema* from(int handle) { return reinterpret_cast<LazySema*>(static_cast<intptr_t>(handle)); }

int32_t kernel_id(LazySema* s) {
    if (s->id >= 0) return s->id;
    ee_sema_t p{};
    p.init_count = s->initial;
    p.max_count = 32767;
    int32_t made = CreateSema(&p);
    if (made < 0) return made;
    DI();
    int32_t current = s->id;
    if (current < 0) s->id = made;
    EI();
    if (current < 0) return made;
    DeleteSema(made);
    return current;
}

}

extern "C" {

int __real_pte_osSemaphoreDelete(int handle);
int __real_pte_osSemaphorePost(int handle, int count);
int __real_pte_osSemaphorePend(int handle, unsigned int* timeout);
int __real_pte_osSemaphoreCancellablePend(int handle, unsigned int* timeout);

int __wrap_pte_osSemaphoreCreate(int initial, int* handle) {
    LazySema* s = new (std::nothrow) LazySema{-1, initial};
    if (!s) return PTE_OS_NO_RESOURCES;
    *handle = static_cast<int>(reinterpret_cast<intptr_t>(s));
    return PTE_OS_OK;
}

int __wrap_pte_osSemaphoreDelete(int handle) {
    LazySema* s = from(handle);
    if (s->id >= 0) __real_pte_osSemaphoreDelete(s->id);
    delete s;
    return PTE_OS_OK;
}

int __wrap_pte_osSemaphorePost(int handle, int count) {
    int32_t id = kernel_id(from(handle));
    if (id < 0) return PTE_OS_NO_RESOURCES;
    return __real_pte_osSemaphorePost(id, count);
}

int __wrap_pte_osSemaphorePend(int handle, unsigned int* timeout) {
    int32_t id = kernel_id(from(handle));
    if (id < 0) return PTE_OS_NO_RESOURCES;
    return __real_pte_osSemaphorePend(id, timeout);
}

int __wrap_pte_osSemaphoreCancellablePend(int handle, unsigned int* timeout) {
    int32_t id = kernel_id(from(handle));
    if (id < 0) return PTE_OS_NO_RESOURCES;
    return __real_pte_osSemaphoreCancellablePend(id, timeout);
}

}
