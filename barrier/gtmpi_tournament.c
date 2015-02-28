#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include "gtmpi.h"

/*
    From the MCS Paper: A scalable, distributed tournament barrier with only local spinning

    type round_t = record
        role : (winner, loser, bye, champion, dropout)
	opponent : ^Boolean
	flag : Boolean
    shared rounds : array [0..P-1][0..LogP] of round_t
        // row vpid of rounds is allocated in shared memory
	// locally accessible to processor vpid

    processor private sense : Boolean := true
    processor private vpid : integer // a unique virtual processor index

    //initially
    //    rounds[i][k].flag = false for all i,k
    //rounds[i][k].role = 
    //    winner if k > 0, i mod 2^k = 0, i + 2^(k-1) < P , and 2^k < P
    //    bye if k > 0, i mode 2^k = 0, and i + 2^(k-1) >= P
    //    loser if k > 0 and i mode 2^k = 2^(k-1)
    //    champion if k > 0, i = 0, and 2^k >= P
    //    dropout if k = 0
    //    unused otherwise; value immaterial
    //rounds[i][k].opponent points to 
    //    round[i-2^(k-1)][k].flag if rounds[i][k].role = loser
    //    round[i+2^(k-1)][k].flag if rounds[i][k].role = winner or champion
    //    unused otherwise; value immaterial
    procedure tournament_barrier
        round : integer := 1
	loop   //arrival
	    case rounds[vpid][round].role of
	        loser:
	            rounds[vpid][round].opponent^ :=  sense
		    repeat until rounds[vpid][round].flag = sense
		    exit loop
   	        winner:
	            repeat until rounds[vpid][round].flag = sense
		bye:  //do nothing
		champion:
	            repeat until rounds[vpid][round].flag = sense
		    rounds[vpid][round].opponent^ := sense
		    exit loop
		dropout: // impossible
	    round := round + 1
	loop  // wakeup
	    round := round - 1
	    case rounds[vpid][round].role of
	        loser: // impossible
		winner:
		    rounds[vpid[round].opponent^ := sense
		bye: // do nothing
		champion: // impossible
		dropout:
		    exit loop
	sense := not sense
*/

#define WINNER (0)
#define LOSER (1)
#define BYE (2)
#define CHAMPION (3)
#define DROPOUT (4)
#define NONE (5)

typedef struct round {
  int opponent;
  unsigned char role;
  unsigned char flag;  
} round_t;

static round_t **rounds;
static int num_procs, num_rounds;

/**
 * Log base 2 of integer value, assume val is a power of 2
 */
static int tlog2(int val) {
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

 void gtmpi_init(int num_threads){
  int i, j;

  num_procs = num_threads;
  num_rounds = tlog2(num_procs) + 1;

  /* Allocate rounds data structure memory */
  rounds = (round_t **) malloc(num_procs * sizeof(round_t *));
  
  for (i = 0; i < num_procs; i++) {
    rounds[i] = (round_t *) malloc(num_rounds * sizeof(round_t));

    /* Initialize values in rounds */
    for (j = 0; j < num_rounds; j++) {
      rounds[i][j].flag = 0;
      
      /* Set up the roles */
      if (j > 0) {

        //    winner if k > 0, i mod 2^k = 0, i + 2^(k-1) < P , and 2^k < P
        if ((i % (1 << j) == 0) && (i + (1 << (j-1)) < num_procs) &&
           ((1 << j) < num_procs)) {
          rounds[i][j].role = WINNER;

        //    bye if k > 0, i mode 2^k = 0, and i + 2^(k-1) >= P
        } else if ((i % (1 << j) == 0) && (i + (1 << (j-1)) >= num_procs)) {
          rounds[i][j].role = BYE;

        //    loser if k > 0 and i mode 2^k = 2^(k-1)
        } else if ((i % (1 << j)) == (1 << (j-1))) {
          rounds[i][j].role = LOSER;

        //    champion if k > 0, i = 0, and 2^k >= P
        } else if ((i == 0) && ((1 << j) >= num_procs)) {
          rounds[i][j].role = CHAMPION;

        //    unused otherwise; value immaterial
        } else {
          rounds[i][j].role = NONE;
        }

      //    dropout if k = 0
      } else {
        /* Assume j will never be negative, so this case is j=0 */
        rounds[i][j].role = DROPOUT;
      }

      if (rounds[i][j].role == LOSER) {
        rounds[i][j].opponent = i - (1 << (j-1));
      } else if (rounds[i][j].role == WINNER || 
                 rounds[i][j].role == CHAMPION) {
        rounds[i][j].opponent = i + (1 << (j-1));
      }
      /* Other cases are irrelevant */

    }
  }
}

void gtmpi_barrier() {
  static char sense = 1;

  MPI_Status stat;
  unsigned int round = 1;
  int vpid;
  char exit_loop = 0;

  // MPI controls the IDs that we need to use
  MPI_Comm_rank(MPI_COMM_WORLD, &vpid);

  /* Arrival loop */
  while (!exit_loop) {
    switch (rounds[vpid][round].role) {
      case LOSER:

        // MPI send here
        MPI_Send(&sense, 1, MPI_CHAR, rounds[vpid][round].opponent, 0, MPI_COMM_WORLD);

        //MPI Recv here
        MPI_Recv(&rounds[vpid][round].flag, 1, MPI_CHAR, rounds[vpid][round].opponent, 0, MPI_COMM_WORLD, &stat);

        // break out of loop
        exit_loop = 1;
        break;

      case WINNER:
        // MPI recv here 
        MPI_Recv(&rounds[vpid][round].flag, 1, MPI_CHAR, rounds[vpid][round].opponent, 0, MPI_COMM_WORLD, &stat);
        break;

      case BYE:
        break;

      case CHAMPION:

        //MPI Recv here
        MPI_Recv(&rounds[vpid][round].flag, 1, MPI_CHAR, rounds[vpid][round].opponent, 0, MPI_COMM_WORLD, &stat);

        // MPI send here
        MPI_Send(&sense, 1, MPI_CHAR, rounds[vpid][round].opponent, 0, MPI_COMM_WORLD);

        // break out of loop
        exit_loop = 1;
        break;

      case DROPOUT:
        /* FIXME: This is an error case which we should handle */
        break;

      default:
        /* FIXME: This is an error case which we should handle */
        break;
    }

    if (!exit_loop) {
      round++;
    }
  }

  exit_loop = 0;

  /* Wake up loop*/
  while (!exit_loop) {
    round--;

    switch (rounds[vpid][round].role) {
      case LOSER:
        /* FIXME: This is an error case which we should handle */
        break;

      case WINNER:
        /* Send MPI msg here */
        MPI_Send(&sense, 1, MPI_CHAR, rounds[vpid][round].opponent, 0, MPI_COMM_WORLD);
        break;

      case BYE:
        break;

      case CHAMPION:
        /* FIXME: This is an error case which we should handle */
        break;

      case DROPOUT:
        exit_loop = 1;
        break;

      default:
        /* FIXME: This is an error case which we should handle */
        break;
    }
  }

  sense = !sense;
}

void gtmpi_finalize() {
  int i;
  
  for (i = 0; i < num_procs; i++) {
    free(rounds[i]);
  }

  free(rounds);
}
