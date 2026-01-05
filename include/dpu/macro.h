#pragma once
#include "debug.h"
#include "pptr.h"
#include "macro_common.h"
#include <defs.h>
#include <mram.h>
#include <perfcounter.h>
#include <string.h>

#ifdef DPU_ENERGY
extern uint64_t op_count;
extern uint64_t db_size_count;
extern uint64_t cycle_count;
#endif

static inline int64_t pptr_to_int64(pptr x) {
    int64_t *i64p = (int64_t *)(&x);
    return *i64p;
}

static inline uint64_t pptr_to_uint64(pptr x) {
    uint64_t *u64p = (uint64_t *)(&x);
    return *u64p;
}

static inline pptr int64_to_pptr(int64_t x) {
    pptr* pptrp = (pptr*)(&x);
    return *pptrp;
}

static inline bool equal_pptr(pptr a, pptr b) {
    return a.id == b.id && a.addr == b.addr;
}

static inline bool valid_pptr(pptr x) {
    return x.id < NR_DPUS && x.addr != null_pptr.addr;
}

#define I64_TO_PPTR(x) (*(pptr*)&(x))
#define PPTR_TO_I64(x) (*(int64_t*)&(x))
#define PPTR_TO_U64(x) (*(uint64_t*)&(x))
#define PPTR(x, y) ((pptr){.id = (uint32_t)(x), .addr = (uint32_t)(y)})

#define EQUAL_PPTR(x, y) (PPTR_TO_I64(x) == PPTR_TO_I64(y))
#define VALID_PPTR(x, nr_of_dpus) (x.id < (uint32_t)nr_of_dpus)

#define MIN(a, b) (((a)<(b))?(a):(b))

typedef __mram_ptr pptr* mppptr;
typedef __mram_ptr int64_t* mpint64_t;
typedef __mram_ptr uint8_t* mpuint8_t;
typedef __mram_ptr void* mpvoid;

#define NULL_pt(type) ((type)-1)

#define M2M_CACHE_SIZE (256)
#define MRAM_OP_SIZE (2048)

static inline void mram_to_mram(__mram_ptr void* dst, __mram_ptr void* src,
                                int len) {
    __dma_aligned uint8_t cache[M2M_CACHE_SIZE];
    int inslen = 0;
    while (inslen < len) {
        int curlen = MIN(M2M_CACHE_SIZE, len - inslen);
        IN_DPU_ASSERT((curlen % 8) == 0, "mram to mram: invalid curlen\n");
        mram_read(src + inslen, cache, curlen);
        mram_write(cache, dst + inslen, curlen);
        inslen += curlen;
#ifdef DPU_ENERGY
        op_count += 2;
#endif
    }
#ifdef DPU_ENERGY
    db_size_count += len + len;
#endif
}

static inline void print_mram_array(char* name, __mram_ptr int64_t* arr, int length) {
    for (int i = 0; i < length; i++) {
        printf("%s[%d] = %lld\n", name, i, arr[i]);
    }
}

static inline void print_array(char* name, int64_t* arr, int length) {
    for (int i = 0; i < length; i++) {
        printf("%s[%d] = %lld\n", name, i, arr[i]);
    }
}

static inline bool ordered(int64_t* arr, int len) {
    for (int j = 0; j < len - 1; j++) {
        if (arr[j] > arr[j + 1]) {
            return false;
        }
    }
    return true;
}

static inline void m_read(__mram_ptr void* mptr, void* ptr, int size) {
    for (int i = 0; i < size; i += MRAM_OP_SIZE) {
        int cursize = MIN(MRAM_OP_SIZE, size - i);
        mram_read(mptr, ptr, cursize);
        mptr += MRAM_OP_SIZE;
        ptr += MRAM_OP_SIZE;
#ifdef DPU_ENERGY
        op_count++;
#endif
    }
#ifdef DPU_ENERGY
    db_size_count += size;
#endif
}

static inline void m_read_single(mpvoid mptr, void* ptr, int size) {
    mram_read(mptr, ptr, size);
#ifdef DPU_ENERGY
    op_count ++;
    db_size_count += size;
#endif
}

static inline void m_write(void* ptr, __mram_ptr void* mptr, int size) {
    for (int i = 0; i < size; i += MRAM_OP_SIZE) {
        int cursize = MIN(MRAM_OP_SIZE, size - i);
        mram_write(ptr, mptr, cursize);
        mptr += MRAM_OP_SIZE;
        ptr += MRAM_OP_SIZE;
#ifdef DPU_ENERGY
        op_count++;
#endif
    }
#ifdef DPU_ENERGY
    db_size_count += size;
#endif
}

static inline void m_write_single(void* ptr, mpvoid mptr, int size) {
    mram_write(ptr, mptr, size);
#ifdef DPU_ENERGY
    op_count ++;
    db_size_count += size;
#endif
}