
#ifndef _LOCK_H
#define _LOCK_H

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <windows.h>
#include <synchapi.h>
#elif __linux__
#include <pthread.h>
#endif

#include <stdint.h>

struct Lock {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  SRWLOCK win_lock; 
#elif __linux__
  pthread_spinlock_t lin_lock;
#endif
};

struct LockCondition {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  CONDITION_VARIABLE win_cond; 
#elif __linux__
  pthread_cond_t lin_cond;
#endif
};

void lock_init(struct Lock *l);
void lock_destroy(struct Lock *l);
void lock_lock(struct Lock *l);
// Returns 0 on fail, 1 on success
int lock_trylock(struct Lock *l);
void lock_unlock(struct Lock *l);

void lock_condition_init(struct LockCondition *lc);
void lock_condition_destroy(struct LockCondition *lc);

// Try to wake up exactly one thread waiting on condition
void lock_condition_signal(struct LockCondition *lc);
// Wake up all threads waiting on condition
void lock_condition_broadcast(struct LockCondition *lc);

// Wait on a condition, WILL aquire lock, but will need to manually check if condition is satisfied
void lock_condition_wait(struct Lock *l, struct LockCondition *lc);

// Wait on a condition with timeout, WILL aquire lock, but will need to manually check if condition is satisfied
void lock_condition_timedwait(struct Lock *l, struct LockCondition *lc, int64_t milliseconds);

#endif