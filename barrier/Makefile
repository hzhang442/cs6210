#OpenMP Flags Etc.
OMPFLAGS = -g -Wall -fopenmp -DLEVEL1_DCACHE_LINESIZE=`getconf LEVEL1_DCACHE_LINESIZE`
OMPLIBS = -lgomp
CC = gcc

#MPI Flags Etc (may need to customize)
#MPICH = /usr/lib64/openmpi/1.4-gcc
MPIFLAGS = -g -Wall #-I$(MPICH)/include
MPILIBS =
#MPICC = /opt/openmpi-1.4.3-gcc44/bin/mpicc
MPICC = mpicc

hello_openmp: hello_openmp.c
	$(CC) $(OMPFLAGS) -o $@ $^ $(OMPLIBS)

hello_gtmp_counter: hello_gtmp.c gtmp_counter.c
	$(CC) $(OMPFLAGS) -o $@ $^ $(OMPLIBS)

hello_gtmp_mcs: hello_gtmp.c gtmp_mcs.c
	$(CC) $(OMPFLAGS) -o $@ $^ $(OMPLIBS)

hello_gtmp_tree: hello_gtmp.c gtmp_tree.c
	$(CC) $(OMPFLAGS) -o $@ $^ $(OMPLIBS)

hello_gtmp_tree_orig: hello_gtmp.c gtmp_tree_orig.c
	$(CC) $(OMPFLAGS) -o $@ $^ $(OMPLIBS)

hello_mpi: hello_mpi.c
	$(MPICC) $(MPIFLAGS) -o $@ $^ $(MPILIBS)

hello_gtmpi_dissemination: hello_gtmpi.c gtmpi_dissemination.c
	$(MPICC) $(MPIFLAGS) -o $@ $^ $(MPILIBS)

clean:
	rm -rf *.o hello_openmp hello_mpi_* hello_gtmp_* hello_gtmpi_*
