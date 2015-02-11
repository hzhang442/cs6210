#include <omp.h>
#include "gtmp.h"

/*
    From the MCS Paper: A sense-reversing centralized barrier

    shared count : integer := P
    shared sense : Boolean := true
    processor private local_sense : Boolean := true

    procedure central_barrier
        local_sense := not local_sense // each processor toggles its own sense
	if fetch_and_decrement (&count) = 1
	    count := P
	    sense := local_sense // last processor toggles global sense
        else
           repeat until sense = local_sense
*/

static unsigned char sense = 0;
static unsigned int count;
static unsigned int my_num_threads;

void gtmp_init(int num_threads){
  my_num_threads = count = num_threads;
}

void gtmp_barrier(){
  //FIXME: maybe put the local sense on its own cache line?
  /* posix_memalign */
  /* LEVEL1_DCACHE_LINESIZE */
  unsigned char my_sense = sense ^ 1;

  // Atomic decrement
  if (__sync_fetch_and_sub(&count, 1) == 1) {
    count = my_num_threads;
    sense = my_sense;
  } else {
    // spin!
    while (sense != my_sense) {
      // do nothing
    }
  }
}

void gtmp_finalize(){
  //FIXME: anything to do here?
}
