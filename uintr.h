#pragma once
#include <unistd.h>
#include <x86gprintrin.h>
#include <uintrintrin.h> /* gcc >= 13 shipped with Ubuntu commented this out */
#include <cstdint>
#include <pthread.h>
#include <stdlib.h>
#include <malloc.h>
#include <unordered_map>
#include <mutex>
#include <atomic>

#ifdef USE_LIBUINTRDRIV
#include <uintrdriv.h>

#define uintr_register_handler(handler, stack, stack_size, flags)              \
  (uintr_register_handler(handler, stack, stack_size, flags))
#define uintr_unregister_handler(receiver_id)                                  \
  (uintr_unregister_handler(receiver_id))
#define uintr_register_sender(receiver_id, vector, flags)                      \
  (uintr_register_sender(receiver_id, vector, flags))
#define uintr_unregister_sender(idx) (uintr_unregister_sender(idx))
#define uintr_wait(flags) (noop())
#define uintr_create_fd(flags) (noop())

#else /* using Intel patched kernel */

#ifndef __NR_uintr_register_handler
#define __NR_uintr_register_handler   471
#define __NR_uintr_unregister_handler 472
#define __NR_uintr_create_fd          473
#define __NR_uintr_register_sender    474
#define __NR_uintr_unregister_sender  475
#define __NR_uintr_wait               476
#endif

#define uintr_register_handler(handler, flags)  syscall(__NR_uintr_register_handler, handler, flags)
#define uintr_unregister_handler(flags)         syscall(__NR_uintr_unregister_handler, flags)
#define uintr_create_fd(vector, flags)          syscall(__NR_uintr_create_fd, vector, flags)
#define uintr_register_sender(fd, flags)        syscall(__NR_uintr_register_sender, fd, flags)
#define uintr_unregister_sender(ipi_idx, flags) syscall(__NR_uintr_unregister_sender, ipi_idx, flags)
#define uintr_wait(flags)                       syscall(__NR_uintr_wait, flags)

#endif

#define NUM_CONTEXTS 2
#define MAX_CORES 64

#if 0
#define SPACE
#define mymalloc malloc SPACE
#define myfree free SPACE
#define malloc(size) ({ \
  pcontext::lock(); \
  void* p = mymalloc(size); \
  pcontext::unlock(); \
  p; \
})
#define free(p) do { \
  pcontext::lock(); \
  myfree(p); \
  pcontext::unlock(); \
} while (0)
#endif

// Spinlock class definition
class Spinlock {
  std::atomic_flag flag = ATOMIC_FLAG_INIT;
public:
  void lock() {
    while (flag.test_and_set(std::memory_order_acquire)) {
      // Busy-wait loop
    }
  }

  bool try_lock() {
    return !flag.test_and_set(std::memory_order_acquire);
  }

  void unlock() {
    flag.clear(std::memory_order_release);
  }
};

struct alignas(64) pcontext {
  pcontext();

  char xsave_area[8192];
	void *reg[1]; // reg[0] = rsp
  void *context_data_oid;
  bool xid_epoch_context_initialized = false;
  bool new_context = false;
  uint64_t stack_start;
  uint64_t stack_end;
  uint64_t fs;
  uint64_t gs;
  uint64_t start_timestamp;
  uint64_t preempted_cycles;

  void SetRSP(void *rsp);

  bool ValidRSP(void *rsp);

  void xsave();

  void xrstor();

  void reset_timer();

  void add_preempted_time(uint64_t cycles);

  bool starved();
  
  static pcontext* get_current_context();

  static void set_current_context(pcontext* ctx);

  static void Set_Worker_Id(uint32_t id);

  static uint64_t get_lock_counter();

  static void set_lock_counter(uint64_t counter);

  static bool locked();

  static void lock();

  static void unlock();
};

static inline void* locked_malloc(std::size_t size) {
  pcontext::lock();
  void* p = std::malloc(size);    // call the real allocator
  pcontext::unlock();
  return p;
}

static inline void  locked_free(void* p) {
  pcontext::lock();
  std::free(p);
  pcontext::unlock();
}

#ifdef malloc
#undef malloc
#endif
#ifdef free
#undef free
#endif

#define malloc(sz) locked_malloc(sz)
#define free(p)   locked_free(p)

extern pcontext* curr_ctx[MAX_CORES];

uint32_t GetWorkerId();

extern "C" void* handler_helper(void* rsp);

extern "C" void* init_stack(void* st, void* fn) asm("init_stack");

extern "C" void __attribute__((interrupt)) __attribute__((noinline))
__attribute__((target("general-regs-only", "inline-all-stringops")))
interrupt_handler_func(struct __uintr_frame *ui_frame, unsigned long long vector);

extern "C" void swap_context(void *current_context, void *next_context) asm("swap_context");

inline void *operator new (size_t size) {
  void *p = malloc(size);
  return p;
}

inline void operator delete (void *p) noexcept {
  free(p);
}
