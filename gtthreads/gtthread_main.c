#include <stdio.h>
#include <stdlib.h>
#include "gtthread.h"

#define LOOP_MAX 300000
#define YIELD_VAL (LOOP_MAX / 3)

/* Tests creation.
   Should print "Hello World!" */

unsigned long int count = 0;
gtthread_mutex_t count_lock;


void *thr1(void *in) {
  int i, j = 0;

  printf("thr1 started with parameter %d\n", (int) in);
  fflush(stdout);

  for (i = 0; i < LOOP_MAX; i++) { 
    printf("thr1!\n");
    fflush(stdout);
    j++;

    gtthread_mutex_lock(&count_lock);
    count++;
    gtthread_mutex_unlock(&count_lock);

    if (i % YIELD_VAL == 0) {
      gtthread_yield();
    }
  }

  printf("thr1 finished with parameter %d\n", (int) in);
  fflush(stdout);

  return (void *) j + 1;
}

void *thr5(void *in) {
  int i, j = 0;

  for (i = 0; i < LOOP_MAX; i++) { 
    printf("thr5!\n");
    fflush(stdout);
    j++;

    if (i % YIELD_VAL == 0) {
      gtthread_yield();
    }
  }

  return (void *) 5;
}

void *thr2(void *in) {
  int i, j = 0;
  void *ret;
  gtthread_t t5;

  printf("thr2 started with parameter %d\n", (int) in);
  fflush(stdout);

  gtthread_create(&t5, thr5, NULL);

  for (i = 0; i < (LOOP_MAX + (LOOP_MAX / 2)); i++) { 
    printf("thr2!\n");
    fflush(stdout);
    j++;
    if (i % YIELD_VAL == 0) {
      gtthread_yield();
    }
  }

  gtthread_join(t5, NULL);
  printf("thr5 joined thr2 with value %d!\n", (int) ret);
  fflush(stdout);

  printf("thr2 finished with parameter %d\n", (int) in);
  fflush(stdout);

  return (void *) j + 2;
}

void *thr4(void *in) {
  int i, j = 0;

  printf("thr4 started with parameter %d\n", (int) in);
  fflush(stdout);

  for (i = 0; i < LOOP_MAX; i++) { 
    printf("thr4!\n");
    fflush(stdout);
    j++;

    gtthread_mutex_lock(&count_lock);
    count++;
    gtthread_mutex_unlock(&count_lock);

    if (i % YIELD_VAL == 0) {
      gtthread_yield();
    }
  }

  printf("thr4 finished with parameter %d\n", (int) in);
  fflush(stdout);

  return (void *) j + 4;
}

void *thr3(void *in) {
  int i, j = 0;
  void *ret;
  gtthread_t t4;

  printf("thr3 started with parameter %d\n", (int) in);
  fflush(stdout);

  gtthread_create(&t4, thr4, (void *) 4);

  for (i = 0; i < LOOP_MAX; i++) { 
    printf("thr3!\n");
    fflush(stdout);
    j++;

    if (i % YIELD_VAL == 0) {
      gtthread_yield();
    }

  }

  gtthread_join(t4, &ret);
  printf("thr4 joined thr3 with value %d!\n", (int) ret);
  fflush(stdout);

  printf("thr3 finished with parameter %d\n", (int) in);
  fflush(stdout);

  return (void *) j + 3;
}

void *thr6(void *in) {
  int i, j = 0;

  printf("thr6 started with parameter %d\n", (int) in);
  fflush(stdout);

  for (i = 0; i < LOOP_MAX; i++) { 
    printf("thr6!\n");
    fflush(stdout);
    j++;

    gtthread_mutex_lock(&count_lock);
    count++;
    gtthread_mutex_unlock(&count_lock);

    if (i % YIELD_VAL == 0) {
      gtthread_yield();
    }
  }

  printf("thr6 finished with parameter %d\n", (int) in);
  fflush(stdout);

  return (void *) j + 4;
}


int main() {
  int i;
  gtthread_t t1, t2, t3, t6;
  void *ret;

  gtthread_mutex_init(&count_lock);

  gtthread_init(5);
  gtthread_create(&t1, thr1, (void *) 1);
  gtthread_create(&t2, thr2, (void *) 2);
  gtthread_create(&t3, thr3, (void *) 3);
  gtthread_create(&t6, thr6, (void *) 6);

  for (i = 0; i < LOOP_MAX; i++) {
    printf("Main thread!\n");
    fflush(stdout);

    gtthread_mutex_lock(&count_lock);
    count++;
    gtthread_mutex_unlock(&count_lock);

    if (i % YIELD_VAL == 0) {
      gtthread_yield();
    }
  }

  printf("main waiting for thr1...\n");
  fflush(stdout);
  gtthread_join(t1, &ret);
  printf("thr1 joined main with value %d!\n", (int) ret);
  fflush(stdout);

  gtthread_cancel(t2);
  printf("thr2 cancelled!\n");
  fflush(stdout);

  printf("main waiting for thr3...\n");
  fflush(stdout);
  gtthread_join(t3, &ret);
  printf("thr3 joined main with value %d!\n", (int) ret);
  fflush(stdout);

  printf("main waiting for thr6...\n");
  fflush(stdout);
  gtthread_join(t6, &ret);
  printf("thr6 joined main with value %d!\n", (int) ret);
  fflush(stdout);

  gtthread_mutex_lock(&count_lock);
  printf("Final count value: %lu\n", count);
  fflush(stdout);
  gtthread_mutex_unlock(&count_lock);

  gtthread_mutex_destroy(&count_lock);
  return EXIT_SUCCESS;
}
