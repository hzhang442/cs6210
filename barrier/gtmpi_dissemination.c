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

static int num_procs, num_rounds;
static char sense_init, parity_init;

/**
 * Ceiling of log base 2 for an integer value
 */
static float ceillog2(int val) {
  int i = 0;

  if (val > 0) {
    while (val >> i != 1) {
      i++;
    }
  } else {
    //we should throw an exception here
  }

  return ((1 << i) == val) ? i : i+1;
}

void gtmpi_init(int num_threads){
  num_procs = num_threads;
  num_rounds = ceillog2(num_procs);
  sense_init = 1;
  parity_init = 0;
}

void gtmpi_barrier(){
  int parity = parity_init;
  char sense = sense_init;
  int vpid; 
  int i, src, dst;
  MPI_Status stat;

  // MPI controls the IDs that we need to use
  MPI_Comm_rank(MPI_COMM_WORLD, &vpid);

  for (i = 0; i < num_rounds; i++) {
    // do MPI send here
    dst = (vpid + (1<<i)) % num_procs;
    MPI_Send(&sense, 1, MPI_CHAR, dst, 0, MPI_COMM_WORLD);

    // do MPI recv here
    src = ((vpid - (1<<i)) + num_procs) % num_procs;
    MPI_Recv(&sense, 1, MPI_CHAR, src, 0, MPI_COMM_WORLD, &stat);
  }

  if (parity == 1) {
    sense_init = !sense_init;
  }

  parity_init = (parity + 1) % 2;
}

void gtmpi_finalize(){
}
