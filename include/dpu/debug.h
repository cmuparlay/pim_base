#pragma once

#include <stdlib.h>
#include "settings.h"
#include "task_framework_common.h"

#ifdef KHB_DEBUG_TASK
#define TASK_IN_DPU_ASSERT(x, y) IN_DPU_ASSERT(x, y)
#define TASK_IN_DPU_ASSERT_EXEC(x, y) IN_DPU_ASSERT_EXEC(x, y)
#else
#define TASK_IN_DPU_ASSERT(x, y) {}
#define TASK_IN_DPU_ASSERT_EXEC(x, y) {}
#endif

#ifdef KHB_DEBUG_SPACE
#define SPACE_IN_DPU_ASSERT(x) assert(x)
#else
#define SPACE_IN_DPU_ASSERT(x) {}
#endif

#ifdef KHB_DEBUG_L3
#define L3_IN_DPU_ASSERT(x, y) IN_DPU_ASSERT(x, y)
#else
#define L3_IN_DPU_ASSERT(x, y) {}
#endif

#ifdef KHB_DEBUG_L2
#define L2_IN_DPU_ASSERT(x, y) IN_DPU_ASSERT(x, y)
#else
#define L2_IN_DPU_ASSERT(x, y) {}
#endif

extern __mram_ptr uint8_t* send_buffer;

#ifdef KHB_DEBUG_CORE
#define EXIT() {printf("tasklet-%d: EXIT()!", me()); (*(__mram_ptr int64_t*)send_buffer) = DPU_BUFFER_ERROR; exit(0); }
#else
#define EXIT() {}
#endif

#ifdef KHB_DEBUG
#define IN_DPU_ASSERT(x, y) {if(!(x)){printf("tasklet-%d: %s", me(), (y));(*(__mram_ptr int64_t*)send_buffer) = DPU_BUFFER_ERROR;exit(0);}}
#define IN_DPU_ASSERT_EXEC(x, y) {if(!(x)){y;(*(__mram_ptr int64_t*)send_buffer) = DPU_BUFFER_ERROR;exit(0);}}
#define DEBUG(x) {x;}
#else
#define IN_DPU_ASSERT(x, y) {}
#define IN_DPU_ASSERT_EXEC(x, y) {}
#define DEBUG(x) {}
#endif