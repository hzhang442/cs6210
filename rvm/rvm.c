#include"rvm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define PATH_BUF_SIZE       (128)
#define SEGNAME_SIZE        (128)
#define REDO_PATH_BUF_SIZE  (PATH_BUF_SIZE + 16)

static rvm_t rvm;

int keyeq(seqsrchst_key a, seqsrchst_key b) {
  return strcmp((char *) a, (char *) b);
}

/*
  Initialize the library with the specified directory as backing store.
*/
rvm_t rvm_init(const char *directory){
  char redopath[REDO_PATH_BUF_SIZE];
  FILE *f;
  struct stat st = {0};

  /* Only make the directory if it does not exist */
  if (stat(directory, &st) == -1) {
    mkdir(directory, 0744);
  }

  strncpy(rvm->prefix, directory, (PATH_BUF_SIZE - 1));

  /* Initialize data structures too */
  seqsrchst_init(&(rvm->segst), keyeq);

  /* Create redo log file */
  strcpy(redopath, rvm->prefix);
  strcpy(&(redopath[strlen(redopath)]), "/redo.log");
  f = fopen(redopath, "w+");
  rvm->redofd = fileno(f);
}

/*
  map a segment from disk into memory. If the segment does not already exist, then create it and give it size size_to_create. If the segment exists but is shorter than size_to_create, then extend it until it is long enough. It is an error to try to map the same segment twice.
*/
void *rvm_map(rvm_t rvm, const char *segname, int size_to_create){
  segment_t seg;

  /* Check if segment exists by name */
  if (seqsrchst_contains(&(rvm->segst), (seqsrchst_key) segname)) {
    seg = (segment_t) seqsrchst_get(&(rvm->segst), (seqsrchst_key) segname);

    /* Check if the segment size is too small */
    if (seg->size < size_to_create) {
      /* Realloc */
      if ((seg->segbase = realloc(seg->segbase, size_to_create)) == NULL) {
        /* We failed, so return error */
        return (void *) -1;
      } 
    }
  } else {
    seg = malloc(sizeof(struct _segment_t));
    strncpy(seg->segname, segname, SEGNAME_SIZE-1);
    seg->size = size_to_create;

    /* If no, malloc memory, create log file, and put into data struct */
    if ((seg->segbase = malloc(size_to_create)) == NULL) {
      /* We failed, so return error */
      return (void *) -1;
    } 
  }

  /* FIXME: make sure that this is the right thing to return */
  return seg->segbase;
}

/*
  unmap a segment from memory.
*/
void rvm_unmap(rvm_t rvm, void *segbase){

  /* FIXME: are these the steps for destroy? */
  /* Look up memory by segbase in data structure */
  /* Remove that instance */
  /* Do linux unmap on log */
  /* Clean up log file handle */
  /* Free memory */
}

/*
  destroy a segment completely, erasing its backing store. This function should not be called on a segment that is currently mapped.
 */
void rvm_destroy(rvm_t rvm, const char *segname){

}

/*
  begin a transaction that will modify the segments listed in segbases. If any of the specified segments is already being modified by a transaction, then the call should fail and return (trans_t) -1. Note that trant_t needs to be able to be typecasted to an integer type.
 */
trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases){


}

/*
  declare that the library is about to modify a specified range of memory in the specified segment. The segment must be one of the segments specified in the call to rvm_begin_trans. Your library needs to ensure that the old memory has been saved, in case an abort is executed. It is legal call rvm_about_to_modify multiple times on the same memory area.
*/
void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size){


}

/*
commit all changes that have been made within the specified transaction. When the call returns, then enough information should have been saved to disk so that, even if the program crashes, the changes will be seen by the program when it restarts.
*/
void rvm_commit_trans(trans_t tid){

}

/*
  undo all changes that have happened within the specified transaction.
 */
void rvm_abort_trans(trans_t tid){


}

/*
 play through any committed or aborted items in the log file(s) and shrink the log file(s) as much as possible.
*/
void rvm_truncate_log(rvm_t rvm){


}

