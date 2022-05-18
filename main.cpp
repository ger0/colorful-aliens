#include <cstdio>
#include <cstdlib>
#include <mpi.h>
#include <vector>
#include <ctime>
#include <cstdlib>

#include "main.h"

int size, rank, len;
unsigned timestamp = 0;
Type process_state;
MPI_Datatype MPI_PAKIET_T;

struct Entry {
   int   process_index;
   Type  type;
};

void sendPacket(Packet_t &pkt, int destination, int tag) {
    MPI_Send(&pkt, 1, MPI_PAKIET_T, destination, tag, MPI_COMM_WORLD);
}

Packet_t prepareRequest(int index) {
   return Packet_t {
      .timestamp  = timestamp, 
      .type       = process_state,
      .index      = index,
      .src        = rank
   };
}

std::vector<Entry> queues = std::vector<Entry>(HOTEL_COUNT + GUIDE_COUNT);

void alien_procedure() {
   // id hotelu 
   int hotelID = rand();
   // requestujemy do wszystkich procesow
   Packet_t req_packet = prepareRequest(hotelID);
   for (unsigned i = 0; i < size; i++) {
      sendPacket(req_packet, i, REQUEST_H);
   }
   // recv ack od wszystkich
   // update kolejka 
}

void assign_state(int& rank, int& size) {
   // SPRZATACZ 
   if (rank < size * CLEANER_PROC) {
      process_state = CLEANER;
   } // FIOLETOWY KOSMITA
   else if (rank < size * (CLEANER_PROC + RED_PROC)) {
      process_state = ALIEN_RED;
   } // BLEKITNY KOSMITA
   else if (rank < size * (CLEANER_PROC + RED_PROC + BLUE_PROC)) {
      process_state = ALIEN_BLUE;
   }
}

int main(int argc, char **argv) {
   //int* hotel = (int*)malloc(sizeof(int) * HOTELS);
	char processor[100];

	MPI_Init(&argc, &argv);

	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Get_processor_name(processor, &len);

   assign_state(rank, size);

   srand(rank);
	printf("Hello world: %d of %d typ procesu: (%i)\n", rank, size, process_state);
	//sleep(2);
	MPI_Finalize();

   return 0;
}
