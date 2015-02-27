#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include "gtmpi.h"

/*
    From the MCS Paper: The scalable, distributed dissemination barrier with only local spinning.

    type flags = record
        myflags : array [0..1] of array [0..LogP - 1] of Boolean
	partnerflags : array [0..1] of array [0..LogP - 1] of ^Boolean
	
    processor private parity : integer := 0
    processor private sense : Boolean := true
    processor private localflags : ^flags

    shared allnodes : array [0..P-1] of flags
        //allnodes[i] is allocated in shared memory
	//locally accessible to processor i

    //on processor i, localflags points to allnodes[i]
    //initially allnodes[i].myflags[r][k] is false for all i, r, k
    //if j = (i+2^k) mod P, then for r = 0 , 1:
    //    allnodes[i].partnerflags[r][k] points to allnodes[j].myflags[r][k]

    procedure dissemination_barrier
        for instance : integer :0 to LogP-1
	    localflags^.partnerflags[parity][instance]^ := sense
	    repeat until localflags^.myflags[parity][instance] = sense
	if parity = 1
	    sense := not sense
	parity := 1 - parity
*/

typedef struct flags_struct {
  char *(myflags[2]);
  //char (**partnerflags)[2];
} flags_t;

flags_t *allnodes;
//static unsigned int vpid_next;
static int num_procs, num_rounds;
static char sense_init, parity_init;

/**
 * Log base 2 for an integer value
 */
static float tlog2(int val) {
  int i = 0;

  if (val > 0) {
    while (val >> i != 1) {
      i++;
    }
  } else {
    //we should throw an exception here
  }

  return i;
}

/**
 * Ceiling function for a float value
 */
static int tceil(float val) {
  int intval = (int) val;

  return (intval > val) ? (intval + 1) : intval; 
}

static int specialmath(int i, int k, int p) {
  int retval = (i + (2 << k)) % p;
  if (retval < 0) {
    printf("Negative mod!\n");
    fflush(stdout);
  }
  return retval;
}

void gtmpi_init(int num_threads){
  int i, j, k;

  num_procs = num_threads;
  num_rounds = tceil(tlog2(num_procs));

  /* Instantiate allnodes data structure */
  // FIXME: maybe allocate one per cache line to avoid false sharing
  allnodes = malloc(num_procs * sizeof(flags_t));
  
  for (i = 0; i < num_procs; i++) {
    for (j = 0; j < 2; j++) {
      allnodes[i].myflags[j] = (char *) malloc(num_rounds * sizeof(char));
      //allnodes[i].partnerflags[j] = malloc(num_rounds * sizeof(char *));
      for (k = 0; k < num_rounds; k++) {
        allnodes[i].myflags[j][k] = 0;
        //allnodes[i].partnerflags[j][k] = &allnodes[specialmath(i)].myflags[j][k];
      }
    }
  }

  //vpid_next = 0;
  sense_init = 1;
  parity_init = 0;
}

void gtmpi_barrier(){
  flags_t *localflags;
  int parity = parity_init;
  char sense = sense_init;
  //unsigned int vpid = __sync_fetch_and_add(&vpid_next, 1) % num_procs;
  int vpid; 
  int i;
  MPI_Status stat;

  // MPI controls the IDs that we need to use
  MPI_Comm_rank(MPI_COMM_WORLD, &vpid);
  localflags = &allnodes[vpid];

  for (i = 0; i < num_rounds; i++) {
    // do MPI send here
    MPI_Send(&sense, 1, MPI_CHAR, specialmath(vpid, i, num_procs), 0, MPI_COMM_WORLD);
    //*localflags->partnerflags[parity][i] = sense;

    // do MPI recv here
    MPI_Recv(&localflags->myflags[parity][i], 1, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &stat);
    //while(localflags->myflags[parity][i] == sense);
  }

  if (parity == 1) {
    sense_init = !sense_init;
  }

  parity = (parity + 1) % 2;
}

void gtmpi_finalize(){
  int i, j;

  for (i = 0; i < num_procs; i++) {
    for (j = 0; j < 2; j++) {
      free(allnodes[i].myflags[j]);
    }
  }

  free(allnodes);
}
