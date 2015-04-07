#include"rvm.h"

#include <errno.h>
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

static seqsrchst_t *segments;
static redo_t redolog;

int segname_keyeq(seqsrchst_key a, seqsrchst_key b) {
  /* FIXME: make sure this is correct */
  return (strcmp((char *) a, (char *) b) == 0);
}

int segbase_keyeq(seqsrchst_key a, seqsrchst_key b) {
  return (((void *) a) == (void *) b);
}

static void get_file_path(rvm_t rvm, const char *segname, char *path) {
  strncpy(path, rvm->prefix, PATH_BUF_SIZE);
  strcat(path, "/");
  strncat(path, segname, SEGNAME_SIZE);
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
  segments = malloc(sizeof(*segments));

  /* Only make the directory if it does not exist */
  if (stat(directory, &st) == -1) {
    mkdir(directory, 0744);
  }

  strncpy(rvm->prefix, directory, (PATH_BUF_SIZE - 1));

  /* Initialize data structures too */
  seqsrchst_init(segments, segname_keyeq);
  seqsrchst_init(&(rvm->segst), segbase_keyeq);

  /* Create redo log file */
  strcpy(redopath, rvm->prefix);
  strcat(redopath, "/redo.log");
  f = fopen(redopath, "a+");
  rvm->redofd = fileno(f);
  redolog = malloc(sizeof(*redolog));

  return rvm;
}

/*
  map a segment from disk into memory. If the segment does not already exist, then create it and give it size size_to_create. If the segment exists but is shorter than size_to_create, then extend it until it is long enough. It is an error to try to map the same segment twice.
*/
void *rvm_map(rvm_t rvm, const char *segname, int size_to_create){
  char path[PATH_BUF_SIZE + 1 + SEGNAME_SIZE];
  segment_t seg;
  FILE *f;

  /* Do lazy log truncation */
  rvm_truncate_log(rvm);

  /* Get file path for segment */
  get_file_path(rvm, segname, path);

  /* Check if segment exists by name */
  if (seqsrchst_contains(segments, (seqsrchst_key) segname)) {
    seg = (segment_t) seqsrchst_get(segments, (seqsrchst_key) segname);

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

      seg->size = size_to_create;
    }
  } else {
    /* Come here if the segment doesn't exist yet */
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

    seqsrchst_put(segments, (seqsrchst_key) seg->segname, (seqsrchst_value) seg);
  }

  seqsrchst_put(&(rvm->segst), (seqsrchst_key) seg->segbase, (seqsrchst_value) seg->segname);

  /* This should read in a segment file's contents if the file exists */
  if(access(path, F_OK) != -1) {
    /* FIXME: neet to set memory in the realloc case */
    f = fopen(path, "r");
    fread(seg->segbase, sizeof(char), seg->size, f);
    fclose(f);
  } else {
    f = fopen(path, "a+");
    memset(seg->segbase, 0, seg->size);
    fwrite(seg->segbase, sizeof(char), seg->size, f);
    fclose(f);
  }

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
  char path[PATH_BUF_SIZE + 1 + SEGNAME_SIZE];

  /* Get file path for segment */
  get_file_path(rvm, segname, path);

  if (seqsrchst_contains(segments, (seqsrchst_key) segname)) {
    seg = (segment_t) seqsrchst_delete(segments, (seqsrchst_key) segname);
    free(seg->segbase);
    free(seg);

    /* erase backing store */
    remove(path);
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
     
      if (seqsrchst_contains(segments, (seqsrchst_key) segname)) {
        seg = (segment_t) seqsrchst_get(segments, (seqsrchst_key) segname);

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
  int numentries;

  /* Look up segment data structure by segbase */
  if (seqsrchst_contains(&(tid->rvm->segst), (seqsrchst_key) segbase)) {
    segname = (char *) seqsrchst_get(&(tid->rvm->segst), (seqsrchst_key) segbase);

    if (seqsrchst_contains(segments, (seqsrchst_key) segname)) {
      seg = (segment_t) seqsrchst_get(segments, (seqsrchst_key) segname);
    } else {
      /* FIXME: this is an error, right? */
      printf("Hit error condition:  segment name not found\n");
      fflush(stdout);
      return;
    }
  } else { 

    /* FIXME: this is an error, right? */
    printf("Hit error condition:  segment base not part of transaction\n");
    fflush(stdout);
    return;
  }

  /* Verify that transaction is correct for this segment */
  /* Just look at the pointer address... is there a reasonable better way? */
  if (tid != seg->cur_trans) {
    /* FIXME: this is an error, right? */
    printf("This segment is not part of the current transaction\n");
    fflush(stdout);
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

  redolog->numentries++;
  numentries = redolog->numentries;
  redolog->entries = (segentry_t *) realloc(redolog->entries, redolog->numentries * sizeof(segentry_t));
  strcpy(redolog->entries[numentries - 1].segname, segname);
  redolog->entries[numentries - 1].segsize = seg->size;
  redolog->entries[numentries - 1].updatesize = size;
  /* FIXME: For now, assume one single update per area in a transaction */
  redolog->entries[numentries - 1].numupdates = 1;
  redolog->entries[numentries - 1].offsets = (int *) malloc(sizeof(int)); 
  redolog->entries[numentries - 1].offsets[0] = offset;
  redolog->entries[numentries - 1].sizes = (int *) malloc(sizeof(int));
  redolog->entries[numentries - 1].sizes[0] = size;
  redolog->entries[numentries - 1].data = segbase;
}

/*
commit all changes that have been made within the specified transaction. When the call returns, then enough information should have been saved to disk so that, even if the program crashes, the changes will be seen by the program when it restarts.
*/
void rvm_commit_trans(trans_t tid){
  int i, fd, offset, size;
  char *segname;
  segment_t seg;
  mod_t *mod;
  FILE *f;
  char *data;
  int writes;

  fd = tid->rvm->redofd;
  f = fdopen(fd, "a");

  if (f == NULL) {
    printf("Couldn't open file with error %d\n", errno);
  }

  /* Write redo log entries to log segment on disk */
  for (i = 0; i < redolog->numentries; i++) {
    /* Prepare buffer for writing a line to file */
    segname = redolog->entries[i].segname;
    size = redolog->entries[i].sizes[0];
    offset = redolog->entries[i].offsets[0];

    data = (char *) redolog->entries[i].data;

    writes = fprintf(f, "%s,%d,%d,", segname, offset, size);
    writes += fwrite(&(data[offset]), sizeof(char), size, f);
    writes += fprintf(f, "\n");

    /* Clean up in-memory redo log entries */
    free(redolog->entries[i].sizes);
    free(redolog->entries[i].offsets);
  }

  /* Flush the file to disk */
  fflush(f);

  /* Clean up in-memory redo log entries */
  free(redolog->entries);
  redolog->numentries = 0;

  /* For all segments that are part of the transaction */
  for (i = 0; i < tid->numsegs; i++) {
    seg = tid->segments[i];

    /* Clean up undo log */
    while (!steque_isempty(&(seg->mods))) {
      mod = (mod_t *) steque_pop(&(seg->mods));
      free(mod->undo);
      free(mod);
    }

    /* Reset transaction id */
    seg->cur_trans = (trans_t) -1;
  }

  free(tid->segments);
  free(tid);
}

/*
  undo all changes that have happened within the specified transaction.
 */
void rvm_abort_trans(trans_t tid){
  int i;
  segment_t seg;
  mod_t *mod;
  char *data;

  /* For all segments that are part of the transaction */
  for (i = 0; i < tid->numsegs; i++) {
    seg = tid->segments[i];

    data = (char *) seg->segbase;

    /* Apply undo log back to memory and clean it up */
    while (!steque_isempty(&(seg->mods))) {
      mod = (mod_t *) steque_pop(&(seg->mods));
      memcpy(&(data[mod->offset]), mod->undo, (size_t) mod->size);
      free(mod->undo);
      free(mod);
    }

    /* Reset transaction id */
    seg->cur_trans = (trans_t) -1;
  }

  /* FIXME: Clean up in-memory redo log entries */
  for (i = 0; i < redolog->numentries; i++) {
    free(redolog->entries[i].sizes);
    free(redolog->entries[i].offsets);
  }

  free(redolog->entries);
  redolog->numentries = 0;

  free(tid->segments);
  free(tid);
}

/*
 play through any committed or aborted items in the log file(s) and shrink the log file(s) as much as possible.
*/
void rvm_truncate_log(rvm_t rvm){
  FILE *segfile, *logfile;
  int offset, size, ret;
  char redopath[REDO_PATH_BUF_SIZE];
  char segpath[PATH_BUF_SIZE + SEGNAME_SIZE];
  char *segname, *offsetstr, *sizestr, *data;
  char *line = NULL;
  size_t linelen, bufsz;

  //char testbuf[256];

  logfile = fdopen(rvm->redofd, "r");

  if (logfile == NULL) {
    printf("Couldn't get log file handle with error %d\n", errno);
    fflush(stdout);
  }

  /* For every entry in the log file */
  while ((linelen = getline(&line, &bufsz, logfile)) != -1) {
    /* Extract segment name */
    segname = strtok(line, ",");

    /* Extract offset */
    offsetstr = strtok(NULL, ",");
    offset = atoi(offsetstr);

    /* Extract data size */
    sizestr = strtok(NULL, ",");
    size = atoi(sizestr);

    /* Extract data */
    /* FIXME: we should treat the bytes as opaque, and that means there could
       be a \n character in the data */
    data = strtok(NULL, "\n");

    /* Look up the segment path and open the file (if not already open) */
    get_file_path(rvm, segname, segpath);
    segfile = fopen(segpath, "r+");

    if (segfile == NULL) {
      printf("Couldn't get log file handle with error %d\n", errno);
      fflush(stdout);
    }

    /* Write out data to segment file based on log entry */
    ret = fseek(segfile, offset, SEEK_SET);

    if (ret != 0) {
      printf("Couldn't seek in segfile with error %d\n", errno);
      fflush(stdout);
    }

    //memcpy(testbuf, data, 12);
    //testbuf[12] = '\0';
    //printf("Data for file: %s\n", testbuf);
    //fflush(stdout);

    ret = fwrite(data, sizeof(char), size, segfile);

    if (ret != size) {
      printf("Writing out %d bytes of data to %s, expected %d bytes\n", ret, segpath, size);
      fflush(stdout);
    }

    ret = fclose(segfile);

    if (ret != 0) {
      printf("Couldn't close segfile ith error %d\n", errno);
      fflush(stdout);
    }
  }

  /* Clear out log file and close it out */
  ret = fclose(logfile);

  strcpy(redopath, rvm->prefix);
  strcat(redopath, "/redo.log");
  logfile = fopen(redopath, "w");
  rvm->redofd = fileno(logfile);
}
