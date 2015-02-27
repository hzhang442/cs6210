#include <stdio.h>
#include <sys/utsname.h>
#include "mpi.h"
#include "gtmpi.h"


int main(int argc, char **argv)
{
  int my_id, num_processes;
  struct utsname ugnm;
    
  MPI_Init(&argc, &argv);

  MPI_Comm_size(MPI_COMM_WORLD, &num_processes);

  gtmpi_init(num_processes);

  MPI_Comm_rank(MPI_COMM_WORLD, &my_id);

  uname(&ugnm);

  printf("Hello World from thread %d of %d, running on %s.\n", my_id, num_processes, ugnm.nodename);

  gtmpi_barrier();

  printf("Goodbye cruel world from thread %d of %d, running on %s.\n", my_id, num_processes, ugnm.nodename);

  gtmpi_barrier();

  printf("Last test from thread %d of %d, running on %s.\n", my_id, num_processes, ugnm.nodename);

  gtmpi_barrier();

  gtmpi_finalize();

  MPI_Finalize();
  return 0;
}

