/* SPDX-FileCopyrightText: (C) 2026 Gavin John */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "../../include/ownership.h"

/* Real, source-visible tokens mirroring include/pthread.h's own
 * pthread_mutex_locked/pthread_mutex_unlocked pair -- LockAlgebra.h's
 * classifyCall() classifies a lock call from these annotations alone,
 * not from the callee's own name, so this fixture's local prototypes
 * must carry the same real annotation shapes the real declarations do. */
tokdef mutex_unlocked;
tokdef mutex_locked lock_held;

typedef struct mutex mutex_t;
int pthread_mutex_lock(mutex_t * handle(mutex) consume(mutex_unlocked) grant(mutex_locked));
int pthread_mutex_unlock(mutex_t * handle(mutex) consume(mutex_locked) grant(mutex_unlocked));

int balanced(mutex_t *mutex) {
  if (pthread_mutex_lock(mutex) != 0)
    return -1;
  if (pthread_mutex_unlock(mutex) != 0)
    __builtin_unreachable();
  return 0;
}

/* A release whose own return value is exactly the enclosing function's
 * return value propagates a possible failure to its caller rather than
 * swallowing it -- see LockDisciplineChecker.cpp's isDirectReturnOperand.
 * This is src/thread/pthread_mutex.c's real pthread_mutex_setprioceiling
 * shape: lock, mutate, `return pthread_mutex_unlock(mutex);`, with
 * nothing between the call and the return. */
int setprioceiling_style(mutex_t *mutex) {
  int error = pthread_mutex_lock(mutex);
  if (error)
    return error;
  return pthread_mutex_unlock(mutex);
}

/* Real, source-visible tokens for LockDisciplineChecker.cpp's
 * ntlibc.LockDiscipline stage -- see tools/clang/LockHandoffContracts.h
 * and src/thread/pthread_cond.c's own copy of these two macros, which
 * this mirrors. */
#define lock_requires_held_on_entry(argument) \
  __attribute__((annotate("ntlibc_lock_requires_held_on_entry:" #argument)))
#define lock_acquires_for_caller \
  __attribute__((annotate("ntlibc_lock_acquires_for_caller")))

/* cond_wait's mutex argument's index (1) and its
 * lock_requires_held_on_entry() annotation are both significant here:
 * the checker reads that real attribute off this function's own
 * declaration, mirroring src/thread/pthread_cond.c's static cond_wait()
 * helper -- POSIX requires pthread_cond_wait()/pthread_cond_timedwait()'s
 * mutex argument to already be locked on entry, and locked again on
 * every return. Without the annotation, the unlock below would
 * misreport as releasing a lock nobody ever acquired (exactly
 * unsafe.c's unlocked_release, which this is not), and the final relock
 * would misreport as the function leaking a lock it acquired itself,
 * when returning with it held is the entire point. */
int cond_wait(void *cond, mutex_t *mutex, void *absolute)
    lock_requires_held_on_entry(1);
int cond_wait(void *cond, mutex_t *mutex, void *absolute) {
  int error = pthread_mutex_unlock(mutex);
  if (error)
    return error;
  /* ...wait for the condition... */
  return pthread_mutex_lock(mutex);
}

/* cond_wait_cleanup mirrors src/thread/pthread_cond.c's real cleanup
 * handler of the same name, registered with pthread_cleanup_push() to run
 * if a thread is cancelled out of cond_wait(): its only job is to
 * reacquire the mutex on the caller's behalf so cond_wait's "always
 * returns with the mutex held" contract holds even under cancellation.
 * The mutex is reached through a struct field inside the void* argument,
 * not a plain parameter -- the lock_acquires_for_caller annotation on
 * this function's own declaration tags whatever region the acquisition
 * below actually resolves to as exempt at the moment it succeeds,
 * rather than needing to know that region in advance. */
struct cond_cleanup {
  mutex_t *mutex;
  int mutex_held;
};

void cond_wait_cleanup(void *argument) lock_acquires_for_caller;
void cond_wait_cleanup(void *argument) {
  struct cond_cleanup *cleanup = argument;
  if (!cleanup->mutex_held) {
    pthread_mutex_lock(cleanup->mutex);
    cleanup->mutex_held = 1;
  }
}

/* A try-lock, whose success is a NONZERO return -- the opposite of POSIX.
 * lock_succeeds_nonzero is what tells the checker which branch of the call
 * actually acquired; without it the two are swapped, and the failing
 * iteration of the retry loop below is taken for a successful acquisition,
 * making the next iteration look like acquiring an already-held lock.
 *
 * Modelled on a real freestanding kernel's only locking primitive: a
 * compare-and-swap try-lock with no blocking form at all, where every
 * caller is expected to try once and yield on failure. */
tokdef trylock_unheld;
tokdef trylock_held lock_held;

lock_succeeds_nonzero
int try_acquire(mutex_t * handle(mutex) consume(trylock_unheld)
                grant(trylock_held));
void try_release(mutex_t * handle(mutex) consume(trylock_held)
                 grant(trylock_unheld));
void yield_until_interrupt(void);

/* The retry loop this whole qualifier exists for: try, and on failure do
 * something that yields rather than spinning. Each failed attempt leaves
 * the lock exactly as unheld as it found it. */
void trylock_retry_loop(mutex_t *mutex) {
  while (!try_acquire(mutex))
    yield_until_interrupt();
  try_release(mutex);
}

/* A single attempt, released only on the branch that actually took it. */
void trylock_single_attempt(mutex_t *mutex) {
  if (try_acquire(mutex))
    try_release(mutex);
}
