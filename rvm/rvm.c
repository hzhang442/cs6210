#include"rvm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/* Mostly arbitrary constants, extracted from hardcoded vals in rvm.h */
#define PATH_BUF_SIZE       (128)
#define SEGNAME_SIZE        (128)
#define REDO_PATH_BUF_SIZE  (PATH_BUF_SIZE + 16)

int segname_keyeq(seqsrchst_key a, seqsrchst_key b) {
  return strcmp((char *) a, (char *) b);
}

int segbase_keyeq(seqsrchst_key a, seqsrchst_key b) {
  return (*((long int *) a) == *((long int *) b));
}

/*
  Initialize the library with the specified directory as backing store.
*/
rvm_t rvm_init(const char *directory){
  char redopath[REDO_PATH_BUF_SIZE];
  FILE *f;
  struct stat st = {0};
  rvm_t rvm;

  rvm = malloc(sizeof(*rvm));
  rvm->segments = malloc(sizeof(*(rvm->segments)));

  /* Only make the directory if it does not exist */
  if (stat(directory, &st) == -1) {
    mkdir(directory, 0744);
  }

  strncpy(rvm->prefix, directory, (PATH_BUF_SIZE - 1));

  /* Initialize data structures too */
  seqsrchst_init(rvm->segments, segname_keyeq);
  seqsrchst_init(&(rvm->segst), segbase_keyeq);

  /* Create redo log file */
  strcpy(redopath, rvm->prefix);
  strcpy(&(redopath[strlen(redopath)]), "/redo.log");
  f = fopen(redopath, "w+");
  rvm->redofd = fileno(f);

  return rvm;
}

/*
  map a segment from disk into memory. If the segment does not already exist, then create it and give it size size_to_create. If the segment exists but is shorter than size_to_create, then extend it until it is long enough. It is an error to try to map the same segment twice.
*/
void *rvm_map(rvm_t rvm, const char *segname, int size_to_create){
  segment_t seg;

  /* Check if segment exists by name */
  if (seqsrchst_contains(rvm->segments, (seqsrchst_key) segname)) {
    seg = (segment_t) seqsrchst_get(rvm->segments, (seqsrchst_key) segname);

    /* FIXME: is this correct? */
    /* If we are remapping an existing mapped memory location, we need to bail */
    if (seqsrchst_contains(&(rvm->segst), (seqsrchst_key) seg->segbase)) {
      return (void *) -1;
    }

    /* Check if the segment size is too small */
    if (seg->size < size_to_create) {
      /* Realloc */
      if ((seg->segbase = realloc(seg->segbase, size_to_create)) == NULL) {
        /* We failed, so return error */
        return (void *) -1;
      } 
    }
  } else {
    seg = malloc(sizeof(*seg));
    strncpy(seg->segname, segname, SEGNAME_SIZE-1);
    seg->size = size_to_create;
    seg->cur_trans = (trans_t) -1;
    steque_init(&(seg->mods));

    /* If no, malloc memory, create log file, and put into data struct */
    if ((seg->segbase = malloc(size_to_create)) == NULL) {
      /* We failed, so return error */
      return (void *) -1;
    } 

    seqsrchst_put(rvm->segments, (seqsrchst_key) seg->segname, (seqsrchst_value) seg);
  }

  seqsrchst_put(&(rvm->segst), (seqsrchst_key) seg->segbase, (seqsrchst_value) seg->segname);

  /* FIXME: make sure that this is the right thing to return */
  return seg->segbase;
}

/*
  unmap a segment from memory.
*/
void rvm_unmap(rvm_t rvm, void *segbase){
  if (seqsrchst_contains(&(rvm->segst), (seqsrchst_key) segbase)) {
    seqsrchst_delete(&(rvm->segst), (seqsrchst_key) segbase);
  }
}

/*
  destroy a segment completely, erasing its backing store. This function should not be called on a segment that is currently mapped.
 */
void rvm_destroy(rvm_t rvm, const char *segname){
  segment_t seg;

  if (seqsrchst_contains(rvm->segments, (seqsrchst_key) segname)) {
    seg = (segment_t) seqsrchst_delete(rvm->segments, (seqsrchst_key) segname);
    free(seg->segbase);
    free(seg);
    /* FIXME: erase backing store? */
  }
}

/*
  begin a transaction that will modify the segments listed in segbases. If any of the specified segments is already being modified by a transaction, then the call should fail and return (trans_t) -1. Note that trant_t needs to be able to be typecasted to an integer type.
 */
trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases){
  int i;
  void *segbase;
  char *segname;
  segment_t seg;
  trans_t trans;

  /* First, set up the transaction structure */
  if ((trans = (trans_t) malloc(sizeof(*trans))) == NULL) {
    return (trans_t) -1;
  }

  trans->rvm = rvm;
  trans->numsegs = numsegs;
  trans->segments = malloc(numsegs * sizeof(trans->segments));

  /* Add the segments to the transaction, if possible */
  for (i = 0; i < numsegs; i++) {
    segbase = segbases[i];
    
    if (seqsrchst_contains(&(rvm->segst), (seqsrchst_key) segbase)) {
      segname = (char *) seqsrchst_get(&(rvm->segst), (seqsrchst_key) segbase);
     
      if (seqsrchst_contains(rvm->segments, (seqsrchst_key) segname)) {
        seg = (segment_t) seqsrchst_get(rvm->segments, (seqsrchst_key) segname);

        /* There is a current transaction using this segment */
        if ((long int) seg->cur_trans != -1) {
          return (trans_t) -1;
        } else {
          /* Set the mappings between segment and transaction */
          seg->cur_trans = trans;
          trans->segments[i] = seg;
        }

      } else {
        /* Error case: segment not found */
        return (trans_t) -1;
      }
 
    } else {
      /* Error case: segment not mapped */
      return (trans_t) -1;
    }
  }

  return trans;
}

/*
  declare that the library is about to modify a specified range of memory in the specified segment. The segment must be one of the segments specified in the call to rvm_begin_trans. Your library needs to ensure that the old memory has been saved, in case an abort is executed. It is legal call rvm_about_to_modify multiple times on the same memory area.
*/
void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size){
  char *segname, *data_segbase;
  segment_t seg;
  mod_t *mod;

  /* Look up segment data structure by segbase */
  if (seqsrchst_contains(&(tid->rvm->segst), (seqsrchst_key) segbase)) {
    segname = (char *) seqsrchst_get(&(tid->rvm->segst), (seqsrchst_key) segbase);

    if (seqsrchst_contains(tid->rvm->segments, (seqsrchst_key) segname)) {
      seg = (segment_t) seqsrchst_get(tid->rvm->segments, (seqsrchst_key) segname);
    } else {
      /* FIXME: this is an error, right? */
      return;
    }
  } else { 
    /* FIXME: this is an error, right? */
    return;
  }

  /* Verify that transaction is correct for this segment */
  /* Just look at the pointer address... is there a reasonable better way? */
  if (tid != seg->cur_trans) {
    /* FIXME: this is an error, right? */
    return;
  }

  /* Create an undo log entry */
  mod = (mod_t *) malloc(sizeof(mod_t));
  mod->offset = offset;
  mod->size = size;
  mod->undo = malloc(size);
  data_segbase = (char *) segbase;
  memcpy(mod->undo, &(data_segbase[offset]), (size_t) size);
  steque_push(&(seg->mods), mod);

  /* Prepare redo log entry */
}

/*
commit all changes that have been made within the specified transaction. When the call returns, then enough information should have been saved to disk so that, even if the program crashes, the changes will be seen by the program when it restarts.
*/
void rvm_commit_trans(trans_t tid){
  /* For all segments that are part of the transaction */

  /* Write redo log entries to log segment on disk */
  //FIXME: need to somehow note that log segment write is complete?

  /* Clean up in-memory redo log entries */

  /* Clean up undo log */
}

/*
  undo all changes that have happened within the specified transaction.
 */
void rvm_abort_trans(trans_t tid){
  /* For all segments that are part of the transaction */

  /* Apply undo log back to memory */

  /* Clean up in-memory redo log entries */

  /* Clean up undo log */
}

/*
 play through any committed or aborted items in the log file(s) and shrink the log file(s) as much as possible.
*/
void rvm_truncate_log(rvm_t rvm){


}

