/**********************************************************************
gtthread_sched.c.  

This file contains the implementation of the scheduling subset of the
gtthreads library.  A simple round-robin queue should be used.
 **********************************************************************/
/*
  Include as needed
*/

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "gtthread.h"

/* 
   Students should define global variables and helper functions as
   they see fit.
 */

typedef struct gtthread_int_t {
  gtthread_t id; // thread ID
  ucontext_t context;
  void *retval; // return value from thread
  int retcode; // return code from thread
  char cancelreq; // cancel request from another thread
  char completed; // flag indicating if this is completed or not
  steque_t join_queue; // queue for threads that are waiting to join this one
} gtthread_int_t;

/* Thread pool */
static steque_t threads;

/* Thread scheduling queue */
static steque_t run_queue;

/* List of created threads */
static long quantum;
static long int next_id = 1;

/* Alarms and timers */
struct itimerval *timer;
struct sigaction act;
static sigset_t vtalrm;

/* Reschedule all threads that are in finished thread's join queue */
static void reschedule_joined(gtthread_int_t *finished) {
  gtthread_int_t *wait;

  while (!steque_isempty(&finished->join_queue)) {
    wait = (gtthread_int_t *) steque_pop(&finished->join_queue);
    steque_enqueue(&run_queue, wait);
  }
}

/* Schedule the next runnable thread */
static void schedule_next(gtthread_int_t *cur) {
  gtthread_int_t *target;

  do {

    target = (gtthread_int_t *) steque_front(&run_queue);

    /* If we got a cancel request, just comply! */
    if (target->cancelreq) {
      target->completed = 1;

      /* Remove completed thread from run queue */
      steque_pop(&run_queue);

      reschedule_joined(target);
    }

    /* Don't exit loop until we have a non-cancelled thread */
  } while (target->completed);

  swapcontext(&cur->context, &target->context);
}

static void start_wrapper(void *(*start_routine)(void *), void *arg) {
  void *retval;

  /* Unblock alarms */
  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);

  retval = start_routine(arg);
  gtthread_exit(retval);
}

/* Find a thread by its id */
static gtthread_int_t * find_thread(gtthread_t thread) {
  int queue_len;
  int i;
  gtthread_int_t *item;

  queue_len = steque_size(&threads);

  for (i = 0; i < queue_len; i++) {
    item = (gtthread_int_t *) steque_pop(&threads);
    steque_enqueue(&threads, item);
    if (item->id == thread) {
      return item;
    }
  }

  return NULL;
}

void print_run_queue(void) {
  int queue_len;
  int i;
  gtthread_int_t *item;

  queue_len = steque_size(&run_queue);

  for (i = 0; i < queue_len; i++) {
    item = (gtthread_int_t *) steque_pop(&run_queue);
    steque_enqueue(&run_queue, item);
    printf("%d->", (int) item->id);
    fflush(stdout);
  }

  printf("\n");
  fflush(stdout);
}

void alrm_handler(int sig){
  gtthread_int_t *old;
  sigset_t oldset;

  /* Block alarms */
  sigprocmask(SIG_BLOCK, &vtalrm, &oldset);

  /* Put current thread at end of run queue */
  old = (gtthread_int_t *) steque_pop(&run_queue);
  steque_enqueue(&run_queue, old);
  
  /* Start running the new thread */
  schedule_next(old);

  /* Unblock alarms */
  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
}

/* NOTE: Assumes signal has been blocked before entering this call */
gtthread_t unschedule_cur(void) {
  gtthread_int_t *cur;
  cur = (gtthread_int_t *) steque_pop(&run_queue);
  return cur->id;
}

/* NOTE: Assumes signal has been blocked before entering this call */
void swapcur(gtthread_t cur) {
  gtthread_int_t *cur_int, *target;
  cur_int = find_thread(cur);
  target = (gtthread_int_t *) steque_front(&run_queue);
  swapcontext(&cur_int->context, &target->context);
}

/* NOTE: Assumes signal has been blocked before entering this call */
void reschedule_thread(gtthread_t thread) {
  gtthread_int_t *target;
  target = (gtthread_int_t *) find_thread(thread);
  steque_enqueue(&run_queue, target);
}

/*
  The gtthread_init() function does not have a corresponding pthread equivalent.
  It must be called from the main thread before any other GTThreads
  functions are called. It allows the caller to specify the scheduling
  period (quantum in micro second), and may also perform any other
  necessary initialization.  If period is zero, then thread switching should
  occur only on calls to gtthread_yield().

  Recall that the initial thread of the program (i.e. the one running
  main() ) is a thread like any other. It should have a
  gtthread_t that clients can retrieve by calling gtthread_self()
  from the initial thread, and they should be able to specify it as an
  argument to other GTThreads functions. The only difference in the
  initial thread is how it behaves when it executes a return
  instruction. You can find details on this difference in the man page
  for pthread_create.
 */
void gtthread_init(long period){
  gtthread_int_t *mainthread;

  /* Malloc for this thread */
  if ((mainthread = malloc(sizeof(gtthread_int_t))) != NULL){

    /* Initialize queues */
    steque_init(&threads);
    steque_init(&run_queue);

    /* Set up mainthread */
    mainthread->id = next_id++;
    mainthread->cancelreq = 0;
    mainthread->completed = 0;
    steque_init(&mainthread->join_queue);

    /* Set up the context */
    getcontext(&mainthread->context);
    mainthread->context.uc_stack.ss_sp = (char *) malloc(SIGSTKSZ);
    mainthread->context.uc_stack.ss_size = SIGSTKSZ;

    steque_enqueue(&threads, mainthread);
    steque_enqueue(&run_queue, mainthread);

    /* Initialize the scheduling quantum */
    quantum = period;

    if (quantum != 0) {
      /* Setting up the signal mask */
      sigemptyset(&vtalrm);
      sigaddset(&vtalrm, SIGVTALRM);

      /* Setting up the alarm */
      timer = (struct itimerval*) malloc(sizeof(struct itimerval));
      timer->it_value.tv_sec = timer->it_interval.tv_sec = 0;
      timer->it_value.tv_usec = timer->it_interval.tv_usec = quantum;

      /* Setting up the handler */
      memset(&act, '\0', sizeof(act));
      act.sa_handler = &alrm_handler;
      if (sigaction(SIGVTALRM, &act, NULL) < 0) {
        printf("sigaction");
      }

      setitimer(ITIMER_VIRTUAL, timer, NULL);
      sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
    }
  }
  // FIXME: what about the error case?
}


/*
  The gtthread_create() function mirrors the pthread_create() function,
  only default attributes are always assumed.
 */
int gtthread_create(gtthread_t *thread,
		    void *(*start_routine)(void *),
		    void *arg){
  gtthread_int_t *thread_int, *self;
  sigset_t oldset;

  sigprocmask(SIG_BLOCK, &vtalrm, &oldset);

  /* Malloc for this new thread */
  if ((thread_int = malloc(sizeof(gtthread_int_t))) != NULL){
    /* Initialize thread values */
    /* Block alarms */
    thread_int->id = next_id++;
    *thread = thread_int->id;
    thread_int->cancelreq = 0;
    thread_int->completed = 0;
    steque_init(&thread_int->join_queue);

    /* Set up the context */
    getcontext(&thread_int->context);
    thread_int->context.uc_stack.ss_sp = (char *) malloc(SIGSTKSZ);
    thread_int->context.uc_stack.ss_size = SIGSTKSZ;
    self = (gtthread_int_t *) steque_front(&run_queue);
    thread_int->context.uc_link = &self->context;
    makecontext(&thread_int->context, (void (*)(void)) start_wrapper, 2, start_routine, arg);

    /* Add new thread to data structures */
    steque_enqueue(&threads, thread_int);
    steque_enqueue(&run_queue, thread_int);
    /* Unblock alarms */
  }

  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
  //FIXME: error case?
  
  /* Return 0 on success */
  return 0;
}

/*
  The gtthread_join() function is analogous to pthread_join.
  All gtthreads are joinable.
 */
int gtthread_join(gtthread_t thread, void **status){
  gtthread_int_t *target, *self;
  sigset_t oldset;
  
  /* Block alarms */
  sigprocmask(SIG_BLOCK, &vtalrm, &oldset);

  /* Find the thread id */
  target = find_thread(thread);

  if (target != NULL) {

    /* If the target thread isn't complete, need to schedule another thread */
    if (!target->completed) {
      // 2. Pop this thread off of the main run queue
      self = (gtthread_int_t *) steque_pop(&run_queue);

      // 3. Enqueue this thread on the target thread run queue
      steque_enqueue(&target->join_queue, self);

      // 4. Schedule the next thread, since this is no longer in the run queue
      schedule_next(self);

      /* Unblock alarms */
      sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
    } else {
      /* Unblock alarms */
      sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
    }

    // 5. Set status
    if (status != NULL) {
      *status = target->retval;
    }

    return 0;
  } else {
    /* Unblock alarms */
    sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);

    // target thread not found
    return 1;
  }
}

/*
  The gtthread_exit() function is analogous to pthread_exit.
 */
void gtthread_exit(void* retval){
  gtthread_int_t *self;
  sigset_t oldset;

  /* Block alarms */
  sigprocmask(SIG_BLOCK, &vtalrm, &oldset);

  /* Remove the thread from run_queue */
  self = (gtthread_int_t *) steque_pop(&run_queue);

  /* Set the return value */
  self->retval = retval;

  /* Mark thread as completed */
  self->completed = 1;

  /* Reschedule joined threads */
  reschedule_joined(self);

  /* Need to reschedule so we don't just drop back
     into parent context */
  schedule_next(self);

  /* Unblock alarms */
  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
}

/*
  The gtthread_yield() function is analogous to pthread_yield, causing
  the calling thread to relinquish the cpu and place itself at the
  back of the schedule queue.
 */
void gtthread_yield(void){
  gtthread_int_t *old;
  sigset_t oldset;

  /* Block alarms */
  sigprocmask(SIG_BLOCK, &vtalrm, &oldset);

  /* Put current thread at end of run queue */
  old = (gtthread_int_t *) steque_pop(&run_queue);
  steque_enqueue(&run_queue, old);
  
  /* Start running the new thread */
  schedule_next(old);

  /* Unblock alarms */
  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
}

/*
  The gtthread_yield() function is analogous to pthread_equal,
  returning zero if the threads are the same and non-zero otherwise.
 */
int  gtthread_equal(gtthread_t t1, gtthread_t t2){
  return (t1 == t2);
}

/*
  The gtthread_cancel() function is analogous to pthread_cancel,
  allowing one thread to terminate another asynchronously.
 */
int  gtthread_cancel(gtthread_t thread){
  gtthread_int_t *target;
  sigset_t oldset;

  /* Block alarms */
  sigprocmask(SIG_BLOCK, &vtalrm, &oldset);

  /* Find the thread id */
  target = find_thread(thread);

  /* Unblock alarms */
  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);

  if (target != NULL) {
    target->cancelreq = 1;
    return 0; // success
  } else {
    return 1; // failure
  }
}

/*
  Returns calling thread.
 */
gtthread_t gtthread_self(void){
  gtthread_int_t *self;
  sigset_t oldset;

  /* Block alarms */
  sigprocmask(SIG_BLOCK, &vtalrm, &oldset);

  self = (gtthread_int_t *) steque_front(&run_queue);

  /* Unblock alarms */
  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);

  return self->id;
}
