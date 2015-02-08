/**********************************************************************
gtthread_mutex.c.  

This file contains the implementation of the mutex subset of the
gtthreads library.  The locks can be implemented with a simple queue.
 **********************************************************************/

/*
  Include as needed
*/

#include <signal.h>
#include <stdlib.h>

#include "gtthread.h"

static sigset_t vtalrm;

/*
  The gtthread_mutex_init() function is analogous to
  pthread_mutex_init with the default parameters enforced.
  There is no need to create a static initializer analogous to
  PTHREAD_MUTEX_INITIALIZER.
 */
int gtthread_mutex_init(gtthread_mutex_t* mutex){
  steque_init(&mutex->wait_queue);
  mutex->locked = 0;
  /* Setting up the signal mask */
  sigemptyset(&vtalrm);
  sigaddset(&vtalrm, SIGVTALRM);
  return 0;
}

/*
  The gtthread_mutex_lock() is analogous to pthread_mutex_lock.
  Returns zero on success.
 */
int gtthread_mutex_lock(gtthread_mutex_t* mutex){
  gtthread_t thread;
  sigset_t oldset;

  /* Block alarms */
  sigprocmask(SIG_BLOCK, &vtalrm, &oldset);

  if (!mutex->locked) {
    mutex->locked = 1;
  } else {

    // remove this thread from scheduler queue
    thread = unschedule_cur();

    // add this thread to this mutex's wait queue
    steque_enqueue(&mutex->wait_queue, &thread);

    // swap context
    swapcur(thread);

  }

  /* Unblock alarms */
  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);

  return 0;
}

/*
  The gtthread_mutex_unlock() is analogous to pthread_mutex_unlock.
  Returns zero on success.
 */
int gtthread_mutex_unlock(gtthread_mutex_t *mutex){
  gtthread_t *next;
  sigset_t oldset;

  /* Block alarms */
  sigprocmask(SIG_BLOCK, &vtalrm, &oldset);

  if (mutex->locked) {

    // Unlock the mutex
    mutex->locked = 0;

    if (steque_size(&mutex->wait_queue) > 0) {
      // Pop one thread out of the wait queue and put it in the run queue
      next = (gtthread_t *) steque_pop(&mutex->wait_queue);
      reschedule_thread(*next);
    }
  }

  /* Unblock alarms */
  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);

  return 0;
}

/*
  The gtthread_mutex_destroy() function is analogous to
  pthread_mutex_destroy and frees any resourcs associated with the mutex.
*/
int gtthread_mutex_destroy(gtthread_mutex_t *mutex){
  sigset_t oldset;

  /* Block alarms */
  sigprocmask(SIG_BLOCK, &vtalrm, &oldset);

  // Unlock the mutex
  mutex->locked = 0;

  // Destroy the wait queue
  steque_destroy(&mutex->wait_queue);

  //FIXME for now just ignore remaining threads in wait queue

  /* Unblock alarms */
  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
  return 0;
}
