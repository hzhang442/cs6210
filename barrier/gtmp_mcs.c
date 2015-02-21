#include <stdlib.h>
#include <stdio.h>
#include <omp.h>
#include "gtmp.h"

/*
    From the MCS Paper: A scalable, distributed tree-based barrier with only local spinning.

    type treenode = record
        parentsense : Boolean
	parentpointer : ^Boolean
	childpointers : array [0..1] of ^Boolean
	havechild : array [0..3] of Boolean
	childnotready : array [0..3] of Boolean
	dummy : Boolean //pseudo-data

    shared nodes : array [0..P-1] of treenode
        // nodes[vpid] is allocated in shared memory
        // locally accessible to processor vpid
    processor private vpid : integer // a unique virtual processor index
    processor private sense : Boolean

    // on processor i, sense is initially true
    // in nodes[i]:
    //    havechild[j] = true if 4 * i + j + 1 < P; otherwise false
    //    parentpointer = &nodes[floor((i-1)/4].childnotready[(i-1) mod 4],
    //        or dummy if i = 0
    //    childpointers[0] = &nodes[2*i+1].parentsense, or &dummy if 2*i+1 >= P
    //    childpointers[1] = &nodes[2*i+2].parentsense, or &dummy if 2*i+2 >= P
    //    initially childnotready = havechild and parentsense = false
	
    procedure tree_barrier
        with nodes[vpid] do
	    repeat until childnotready = {false, false, false, false}
	    childnotready := havechild //prepare for next barrier
	    parentpointer^ := false //let parent know I'm ready
	    // if not root, wait until my parent signals wakeup
	    if vpid != 0
	        repeat until parentsense = sense
	    // signal children in wakeup tree
	    childpointers[0]^ := sense
	    childpointers[1]^ := sense
	    sense := not sense
*/

typedef struct treenode_s {
  unsigned char parentsense;
  unsigned char *parentpointer;
  unsigned char *childpointers[2];
  unsigned char havechild[4];
  unsigned char childnotready[4];
  unsigned char dummy;
} treenode;

static treenode *nodes;

static unsigned int vpid_next;
static unsigned int sense_init;
static int num_procs;


void gtmp_init(int num_threads){
  int i, j;

  num_procs = num_threads;
  vpid_next = 0;
  sense_init = 1;
  nodes = (treenode *) malloc(num_procs * sizeof(treenode));
  
  for (i = 0; i < num_procs; i++) {
    for (j = 0; j < 4; j++) {
      if (4 * i + j + 1 < num_procs) {
        nodes[i].havechild[j] = 1;
      } else {
        nodes[i].havechild[j] = 0;
      }

      nodes[i].childnotready[j] = nodes[i].havechild[j];
    }

    if (i != 0) {
      /* FIXME: VERIFY: Implicit floor due to cast */
      nodes[i].parentpointer = &(nodes[(i-1)/4].childnotready[(i-1) % 4]);
    } else {
      nodes[i].parentpointer = &(nodes[i].dummy);
    }

    if (2 * i + 1 < num_procs) {
      nodes[i].childpointers[0] = &(nodes[2*i+1].parentsense);
    } else {
      nodes[i].childpointers[0] = &(nodes[i].dummy);
    }

    if (2 * i + 2 < num_procs) {
      nodes[i].childpointers[1] = &(nodes[2*i+2].parentsense);
    } else {
      nodes[i].childpointers[1] = &(nodes[i].dummy);
    }

    nodes[i].parentsense = 0;
  }
}

void gtmp_barrier(){
  //FIXME: do atomic fetch and add; store vpid_next in vpid, then increment vpid_next;
  unsigned int vpid = __sync_fetch_and_add(&vpid_next, 1) % num_procs;
  unsigned char sense = sense_init;
  int i;

  /* If any child is not ready, keep spinning */
  while ((nodes[vpid].childnotready[0] | nodes[vpid].childnotready[1] | 
          nodes[vpid].childnotready[2] | nodes[vpid].childnotready[3])) {
    //spin!
  }

  for (i = 0; i < 4; i++) {
    nodes[vpid].childnotready[i] = nodes[vpid].havechild[i];
  }

  *(nodes[vpid].parentpointer) = 0;

  if (vpid != 0) {
    //printf("%d going to wait...\n", vpid);
    //fflush(stdout);

    while (nodes[vpid].parentsense != sense) {
      //spin!
    }

    //printf("%d waking up...\n", vpid);
    //fflush(stdout);
  }

  *nodes[vpid].childpointers[0] = sense;
  *nodes[vpid].childpointers[1] = sense;

  sense_init = !sense;
}

void gtmp_finalize(){
  free(nodes);
}
