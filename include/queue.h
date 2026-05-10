/*
queue.h — lock-free work-stealing dispatch for embarrassingly parallel agent phases.

Workers sleep on a condvar until world_dispatch wakes them via generation bump.
Workers atomically claim batches with fetch_add on a dedicated-cache-line cursor;
fast cores claim more, slow cores fewer.  Completion uses a second condvar so
the main thread sleeps instead of spinning.

Cursor uses RELAXED ordering (work-stealing, no shared data between batches).
Done-count uses RELEASE on worker final-write and ACQUIRE on main-thread check
so that the agent-field stores are visible before the main thread reads them.
*/

#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include <stdint.h>

#define ATOMIC_FETCH_ADD(ptr, val) __atomic_fetch_add((ptr), (val), __ATOMIC_RELAXED)
#define ATOMIC_FETCH_ADD_REL(ptr, val) __atomic_fetch_add((ptr), (val), __ATOMIC_RELEASE)
#define ATOMIC_LOAD_ACQ(ptr) __atomic_load_n((ptr), __ATOMIC_ACQUIRE)

#define BATCH_SIZE 256

enum {
  QUEUE_PHASE_INPUTS = 0,
  QUEUE_PHASE_OUTPUTS = 1,
};

struct World;

struct Queue {
  // ---- Cold section: accessed only under mutex, once per dispatch ----
  int quit;

  pthread_mutex_t mutex;
  pthread_cond_t work_cond; // workers sleep here
  pthread_cond_t done_cond; // main thread sleeps here

  // Phase context (written by world_dispatch, read by workers — all under mutex)
  uint32_t generation;
  uint32_t phase;
  uint32_t total;
  uint32_t brain_slot;
  struct World *world;
  uint32_t num_participants; // set by main() after thread creation

  // ---- Hot section: lock-free atomics, each on its own cache line ----
  __attribute__((aligned(64))) uint32_t cursor;     // atomic fetch_add
  __attribute__((aligned(64))) uint32_t done_count; // atomic fetch_add
};

void queue_init(struct Queue *q);
void queue_destroy(struct Queue *q);
void queue_close(struct Queue *q);
void world_dispatch(struct Queue *q, uint32_t phase, uint32_t slot);
void *worker_thread(void *arg);

#endif
