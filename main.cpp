#include <cstdio>
#include <cstdlib>
#include <mpi.h>
#include <vector>
#include <ctime>
#include <cstdlib>
#include <pthread.h>

#include "main.h"
#include "watek_komunikacyjny.h"

Type process_state;

std::vector<std::vector<Entry>> queues = std::vector<std::vector<Entry>>(HOTEL_COUNT + GUIDE_COUNT);
pthread_t commThread;

int size, rank, len;
unsigned timestamp = 0;
MPI_Datatype MPI_PAKIET_T;

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

void alien_procedure() {
   // id hotelu 
   int hotelID = rand() % HOTEL_COUNT;
   printf("hotel: %i\n", hotelID);
   // requestujemy do wszystkich procesow
   Packet_t req_packet = prepareRequest(hotelID);
   for (unsigned i = 0; i < size; i++) {
      sendPacket(req_packet, i, REQUEST_H);
   }
   // recv i sortowanie kolejki w watku komunikacyjnym
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

//MPI_Init(&argc, &argv);
   int provided;
   MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);

   /* Stworzenie typu */
   /* Poniższe (aż do MPI_Type_commit) potrzebne tylko, jeżeli
    brzydzimy się czymś w rodzaju MPI_Send(&typ, sizeof(pakiet_t), MPI_BYTE....
   */
   /* sklejone z stackoverflow */
   const int nitems = FIELDNO; /* bo packet_t ma FIELDNO pól */
   int       blocklengths[FIELDNO] = {1,1,1, 1};
   MPI_Datatype typy[FIELDNO] = {MPI_UNSIGNED, MPI_INT, MPI_INT, MPI_INT};

   MPI_Aint     offsets[FIELDNO]; 
   offsets[0] = offsetof(Packet_t, timestamp);
   offsets[1] = offsetof(Packet_t, type);
   offsets[2] = offsetof(Packet_t, index);
   offsets[3] = offsetof(Packet_t, src);

   MPI_Type_create_struct(nitems, blocklengths, offsets, typy, &MPI_PAKIET_T);
   MPI_Type_commit(&MPI_PAKIET_T);
   pthread_create(&commThread, NULL, startKomWatek, 0);

   MPI_Comm_size(MPI_COMM_WORLD, &size);
   MPI_Comm_rank(MPI_COMM_WORLD, &rank);
   MPI_Get_processor_name(processor, &len);
   srand(time(NULL) + rank);

   assign_state(rank, size);
   alien_procedure();

   printf("Hello world: %d of %d typ procesu: (%i)\n", rank, size, process_state);
   pthread_join(commThread, NULL);
   MPI_Type_free(&MPI_PAKIET_T);
   MPI_Finalize();

   return 0;
}
