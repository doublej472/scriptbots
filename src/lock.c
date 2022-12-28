#include <stdint.h>

#include "lock.h"

#if __linux__
#include <time.h>

/* Add a nanosecond value to a timespec
 *
 * \param r[out] result: a + b
 * \param a[in] base operand as timespec
 * \param b[in] operand in nanoseconds
 */
static inline void
timespec_add_nsec(struct timespec *r, const struct timespec *a, int64_t b)
{
	r->tv_sec = a->tv_sec + (b / NSEC_PER_SEC);
	r->tv_nsec = a->tv_nsec + (b % NSEC_PER_SEC);

	if (r->tv_nsec >= NSEC_PER_SEC) {
		r->tv_sec++;
		r->tv_nsec -= NSEC_PER_SEC;
	} else if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += NSEC_PER_SEC;
	}
}

/* Add a millisecond value to a timespec
 *
 * \param r[out] result: a + b
 * \param a[in] base operand as timespec
 * \param b[in] operand in milliseconds
 */
static inline void
timespec_add_msec(struct timespec *r, const struct timespec *a, int64_t b)
{
	timespec_add_nsec(r, a, b * 1000000);
}

#endif

void lock_init(struct Lock *l) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  InitializeSRWLock(&l->win_lock);
#elif __linux__
  pthread_mutex_init(&l->lin_lock, NULL);
#endif
}

void lock_destroy(struct Lock *l) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
//   DeleteCriticalSection(&l->critical_section);
#elif __linux__
  pthread_mutex_destroy(&l->lin_lock);
#endif
}

void lock_lock(struct Lock *l) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  AcquireSRWLockExclusive(&l->win_lock);
#elif __linux__
  pthread_mutex_lock(&l->lin_lock);
#endif
}

int lock_trylock(struct Lock *l) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  return TryAcquireSRWLockExclusive(&l->win_lock) == 0 ? 0 : 1;
#elif __linux__
  return pthread_mutex_trylock(&l->lin_lock) == 0 ? 1 : 0;
#endif
}

void lock_unlock(struct Lock *l) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  ReleaseSRWLockExclusive(&l->win_lock);
#elif __linux__
    pthread_mutex_unlock(&l->lin_lock);
#endif
}

void lock_condition_init(struct LockCondition *lc) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  InitializeConditionVariable(&lc->win_cond);
#elif __linux__
    pthread_cond_init(&lc->lin_cond);
#endif
}

void lock_condition_destroy(struct LockCondition *lc) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  // nothing here
#elif __linux__
    pthread_cond_destroy(&lc->lin_cond);
#endif
}

void lock_condition_signal(struct LockCondition *lc) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  WakeConditionVariable(&lc->win_cond);
#elif __linux__
  pthread_cond_signal(&lc->lin_cond);
#endif
}

void lock_condition_broadcast(struct LockCondition *lc) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  WakeAllConditionVariable(&lc->win_cond);
#elif __linux__
  pthread_cond_broadcast(&lc->lin_cond);
#endif
}

void lock_condition_timedwait(struct Lock *l, struct LockCondition *lc, int64_t milliseconds) {
  #if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  SleepConditionVariableSRW(&lc->win_cond, &l->win_lock, (DWORD) milliseconds, 0);
#elif __linux__
  struct timespec ts, tsfuture;
  clock_gettime(CLOCK_REALTIME, &ts);
  timespec_add_msec(&tsfuture, &ts, (int64_t) milliseconds);
  pthread_cond_timedwait(&lc->lin_cond, &l->lin_lock, &tsfuture);
#endif
}

void lock_condition_wait(struct Lock *l, struct LockCondition *lc) {
  lock_condition_timedwait(l, lc, 3000);
}