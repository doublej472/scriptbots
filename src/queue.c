/*
queue.c — lock-free dispatch implementation.

Workers sleep on work_cond until world_dispatch bumps generation and broadcasts.
The main thread participates directly, then waits on done_cond for completion.
The last worker to finish signals done_cond — no busy-spinning.

Phase parameters are set under the mutex so workers see a consistent snapshot.
The mutex uses PTHREAD_MUTEX_ADAPTIVE_NP (brief spin before futex) since all
critical sections are sub-microsecond.
*/

#include "queue.h"
#include "World.h"
#include "helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h> /* CLOCK_MONOTONIC */

// ---- Lifecycle ----

void queue_init(struct Queue *q) {
  q->quit = 0;
  q->generation = 0;
  q->phase = 0;
  q->total = 0;
  q->brain_slot = 0;
  q->world = NULL;
  q->cursor = 0;
  q->done_count = 0;
  q->num_participants = 1; /* safe default: main thread only */

  /* Adaptive mutex: spins briefly before futex — ideal for sub-µs critical sections */
  pthread_mutexattr_t mattr;
  pthread_mutexattr_init(&mattr);
  pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_ADAPTIVE_NP);
  pthread_mutex_init(&q->mutex, &mattr);
  pthread_mutexattr_destroy(&mattr);

  /* Monotonic clock: immune to NTP / time jumps */
  pthread_condattr_t cattr;
  pthread_condattr_init(&cattr);
  pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);
  pthread_cond_init(&q->work_cond, &cattr);
  pthread_cond_init(&q->done_cond, &cattr);
  pthread_condattr_destroy(&cattr);
}

void queue_destroy(struct Queue *q) {
  pthread_mutex_destroy(&q->mutex);
  pthread_cond_destroy(&q->work_cond);
  pthread_cond_destroy(&q->done_cond);
}

void queue_close(struct Queue *q) {
  pthread_mutex_lock(&q->mutex);
  q->quit = 1;
  pthread_mutex_unlock(&q->mutex);
  pthread_cond_broadcast(&q->work_cond);
}

// ---- Worker entry point ----

void *worker_thread(void *arg) {
  struct Queue *q = (struct Queue *)arg;
  init_thread_random();

  uint32_t my_gen = 0; /* last generation we participated in */

  for (;;) {
    /* Wait for new work — reads phase params with full visibility
     * because the mutex unlock→lock chain synchronises with the
     * main thread's writes inside world_dispatch. */
    pthread_mutex_lock(&q->mutex);
    while (q->generation == my_gen && !q->quit)
      pthread_cond_wait(&q->work_cond, &q->mutex);
    if (q->quit) {
      pthread_mutex_unlock(&q->mutex);
      return NULL;
    }
    my_gen = q->generation;
    uint32_t phase = q->phase;
    uint32_t total = q->total;
    uint32_t slot = q->brain_slot;
    struct World *w = q->world;
    pthread_mutex_unlock(&q->mutex);

    /* Work-stealing: claim batches via lock-free atomic cursor */
    for (;;) {
      uint32_t start = ATOMIC_FETCH_ADD(&q->cursor, BATCH_SIZE);
      if (start >= total)
        break;
      uint32_t end = start + BATCH_SIZE;
      if (end > total)
        end = total;

      switch (phase) {
      case QUEUE_PHASE_INPUTS:
        agent_input_processor_range(w, start, end, slot);
        break;
      case QUEUE_PHASE_OUTPUTS:
        agent_output_processor_range(w, start, end, slot);
        break;
      }
    }

    /* Last worker to finish wakes the main thread */
    uint32_t prev = ATOMIC_FETCH_ADD_REL(&q->done_count, 1);
    if (prev + 1 == q->num_participants) {
      pthread_mutex_lock(&q->mutex);
      pthread_cond_signal(&q->done_cond);
      pthread_mutex_unlock(&q->mutex);
    }
  }
}

// ---- Main-thread dispatch (participates as a worker, then waits for others) ----

void world_dispatch(struct Queue *q, uint32_t phase, uint32_t slot) {
  /*
   * Phase parameters are set UNDER the mutex, then the generation bump +
   * broadcast wake all workers.  The mutex unlock→lock chain guarantees
   * workers see the new parameters (fixes weak-ordering hazard on ARM).
   * The plain done_count = 0 store is likewise covered by the mutex
   * happens-before (not a data race per C11 §5.1.2.4p25).
   */
  pthread_mutex_lock(&q->mutex);
  q->phase = phase;
  q->total = (uint32_t)q->world->agents.size;
  q->brain_slot = slot;
  q->cursor = 0;
  q->done_count = 0;
  q->generation++;
  pthread_cond_broadcast(&q->work_cond);
  pthread_mutex_unlock(&q->mutex);

  /* Main thread participates — reduces dispatch latency */
  uint32_t total = q->total;
  for (;;) {
    uint32_t start = ATOMIC_FETCH_ADD(&q->cursor, BATCH_SIZE);
    if (start >= total)
      break;
    uint32_t end = start + BATCH_SIZE;
    if (end > total)
      end = total;

    switch (phase) {
    case QUEUE_PHASE_INPUTS:
      agent_input_processor_range(q->world, start, end, slot);
      break;
    case QUEUE_PHASE_OUTPUTS:
      agent_output_processor_range(q->world, start, end, slot);
      break;
    }
  }

  /*
   * Completion: condvar wait replaces the old busy-spin.
   * The while-loop guards against lost signals (worker signals before we
   * lock) and spurious wakeups.  The ACQ_REL fetch_add pairs with each
   * worker's RELEASE fetch_add so agent-field writes are visible after
   * dispatch returns (even when the main thread is the last to finish).
   */
  uint32_t prev = __atomic_fetch_add(&q->done_count, 1, __ATOMIC_ACQ_REL);
  if (prev + 1 < q->num_participants) {
    pthread_mutex_lock(&q->mutex);
    while (ATOMIC_LOAD_ACQ(&q->done_count) < q->num_participants)
      pthread_cond_wait(&q->done_cond, &q->mutex);
    pthread_mutex_unlock(&q->mutex);
  }
}
