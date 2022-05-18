#include <cstdio>
#include <cstdlib>
#include <mpi.h>

#include "main.h"

Type process_state;

void assign_state(int& rank, int& size) {
   // SPRZATACZ 
   if (rank < size * CLEANER_PROC) {
      process_state = CLEANER;
   } // FIOLETOWY KOSMITA
   else if (rank < size * (CLEANER_PROC + RED_PROC)) {
      process_state = ALIEN_RED;
   } // BLEKITNY KOSMITA

}

int main(int argc, char **argv) {

   //int* hotel = (int*)malloc(sizeof(int) * HOTELS);
	int size, rank, len;
	char processor[100];
	MPI_Init(&argc, &argv);

	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Get_processor_name(processor, &len);

   assign_state(rank, size);
	printf("Hello world: %d of %d typ procesu: (%i)\n", rank, size, process_state);
	//sleep(2);
	MPI_Finalize();

   return 0;
}
