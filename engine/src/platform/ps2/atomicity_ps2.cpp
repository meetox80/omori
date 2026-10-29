#include <kernel.h>

#include <ext/atomicity.h>

namespace __gnu_cxx {

_Atomic_word __exchange_and_add(volatile _Atomic_word* mem, int val) noexcept {
    int enabled = DIntr();
    _Atomic_word old = *mem;
    *mem = old + val;
    if (enabled) EIntr();
    return old;
}

void __atomic_add(volatile _Atomic_word* mem, int val) noexcept {
    int enabled = DIntr();
    *mem += val;
    if (enabled) EIntr();
}

}

extern "C" unsigned int __atomic_fetch_add_4(volatile void* mem, unsigned int val, int) {
    volatile unsigned int* p = (volatile unsigned int*)mem;
    int enabled = DIntr();
    unsigned int old = *p;
    *p = old + val;
    if (enabled) EIntr();
    return old;
}

extern "C" unsigned int __atomic_fetch_sub_4(volatile void* mem, unsigned int val, int) {
    volatile unsigned int* p = (volatile unsigned int*)mem;
    int enabled = DIntr();
    unsigned int old = *p;
    *p = old - val;
    if (enabled) EIntr();
    return old;
}
