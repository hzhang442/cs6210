#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "rvm.h"

/* proc1 writes some data, commits it, then exits */
void proc1() 
{
  rvm_t rvm;
  trans_t trans;
  char* segs[2];

  rvm = rvm_init("rvm_segments");
  rvm_destroy(rvm, "testseg");
  segs[0] = (char *) rvm_map(rvm, "testseg", 10000);
  segs[1] = (char *) rvm_map(rvm, "test2", 100);
     
  trans = rvm_begin_trans(rvm, 2, (void **) segs);
     
  rvm_about_to_modify(trans, segs[0], 0, 100);
  sprintf(segs[0], "hello, world");
     
  rvm_about_to_modify(trans, segs[0], 1000, 100);
  sprintf(segs[0]+1000, "hello, world");
   
  rvm_about_to_modify(trans, segs[1], 0, 10);
  sprintf(segs[1], "tupty");

  rvm_commit_trans(trans);

  printf("Committed trans\n");
  fflush(stdout);

  trans = rvm_begin_trans(rvm, 1, (void **) &segs[1]);

  printf("Began second trans\n");
  fflush(stdout);

  rvm_about_to_modify(trans, segs[1], 0, 10);

  printf("Ran about to modify in second trans\n");
  fflush(stdout);

  sprintf(segs[1], "tufty");

  printf("About to abort trans\n");
  fflush(stdout);

  rvm_abort_trans(trans);

  printf("Aborted trans\n");
  fflush(stdout);

  abort();
}


/* proc2 opens the segments and reads from them */
void proc2() 
{
  char* segs[2];
  rvm_t rvm;
     
  rvm = rvm_init("rvm_segments");

  segs[0] = (char *) rvm_map(rvm, "testseg", 10000);
  segs[1] = (char *) rvm_map(rvm, "test2", 10000);

  if(strcmp(segs[0], "hello, world")) {
    fprintf(stderr, "A second process did not find what the first had written.\n");
    exit(EXIT_FAILURE);
  }
  if(strcmp(segs[0]+1000, "hello, world")) {
    fprintf(stderr, "A second process did not find what the first had written.\n");
    exit(EXIT_FAILURE);
  }
  if(strcmp(segs[1], "tupty")) {
    fprintf(stderr, "A second process did not find what the first had written.\n");
    exit(EXIT_FAILURE);
  }
}


int main(int argc, char **argv)
{
  int pid;
  pid = fork();
  if(pid < 0) {
    perror("fork");
    exit(2);
  }
  if(pid == 0) {
    proc1();
    exit(EXIT_SUCCESS);
  }

  waitpid(pid, NULL, 0);

  proc2();

  printf("Ok\n");

  return 0;
}
