#include <cstdio>
#include <cstdlib>
#include <mpi.h>
#include <vector>
#include <ctime>
#include <cstdlib>
#include <pthread.h>

#include "main.hpp"
#include "watek_komunikacyjny.hpp"

Type process_state;

std::vector<std::vector<Entry>> queues = std::vector<std::vector<Entry>>(HOTEL_COUNT + GUIDE_COUNT);
pthread_t         commThread;
pthread_mutex_t   queueMutex  = PTHREAD_MUTEX_INITIALIZER;

// zmienna zliczajaca ilosc odebranych ACK
unsigned          acks = 0;
pthread_mutex_t   acksMutex   = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t    acksCond    = PTHREAD_COND_INITIALIZER;

// zmienne okreslajace proces
int               size, rank, len;
unsigned          timestamp = 0;
MPI_Datatype      MPI_PAKIET_T;

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
   /* TODO: zmienic logike wyboru (wybierac hotel dla ktorego wiemy ze ma miejsce
    * w tym samym kolorze */
   // losujemy id hotelu 
   int hotelID = rand() % HOTEL_COUNT;
   debug("Proces o kolorze: %d wybrał hotel %d", (int)process_state, hotelID);
   // spimy randomowa dlugosc 
   usleep(rand() % 500);
   // requestujemy do wszystkich procesow
   ++timestamp;
   Packet_t req_packet = prepareRequest(hotelID);
   acks = 0;
   for (unsigned i = 0; i < size; i++) {
      sendPacket(req_packet, i, REQUEST_H);
   }
   // Odbior ACK i sortowanie kolejki w watku komunikacyjnym
   // kontynuacja kiedy otrzymamy ACK od kazdego procesu jak odpowiedzą 
   pthread_mutex_lock(&acksMutex);
   while (acks < size) {
      pthread_cond_wait(&acksCond, &acksMutex);
   }
   pthread_mutex_lock(&queueMutex);

   bool isDifferentColour = false;
   // debug print kolejki
   {
      debug("  Kolejka dostepu do hotelu %d:", hotelID);
      for (unsigned i = 0; i < queues[hotelID].size(); i++) {
         debug("     [%d], idx: %d, kolor: %d, timestamp: %d", 
               queues[hotelID][i].process_index, i, 
               (int)queues[hotelID][i].type, queues[hotelID][i].timestamp
         );
      }
   }
   /* Sprawdzenie czy w kolejce do hotelu nie ma innego koloru przed obecnym procesem
    * jeżeli nie - proces wchodzi do hotelu */
   for (unsigned i = 0; i < queues[hotelID].size(); i++) {
      if (i < SLOTS_PER_HOTEL) {
         if (queues[hotelID][i].process_index == rank && !isDifferentColour) {
            debug("===== Proces %d wchodzi do hotelu %d o kolorze: %d =====", 
                  rank, hotelID, (int)process_state);      
            break;
         } else if (queues[hotelID][i].type != process_state) {
            debug("Wykryto kosmitę o innym kolorze!");
            isDifferentColour = true; 
            break;
         }
      }
   }
   // Opuszczanie miejsca w kolejce gdy wykryto inny kolor 
   if (isDifferentColour) {
      Packet_t rel_packet = Packet_t{
         .timestamp  = timestamp,
         .type       = process_state,
         .index      = hotelID,
         .src        = rank
      };
      acks = 0;
      timestamp++;
      for (unsigned i = 0; i < size; i++) {
         sendPacket(rel_packet, i, RELEASE);
      }
   }
   pthread_mutex_unlock(&queueMutex);
   pthread_mutex_unlock(&acksMutex);
}

void assign_state(int& rank, int& size) {
   // SPRZATACZ 
   if (rank < size * CLEANER_PROC / 100) {
      process_state = CLEANER;
   } // FIOLETOWY KOSMITA
   else if (rank < size * (CLEANER_PROC + RED_PROC) / 100) {
      process_state = ALIEN_RED;
   } // BLEKITNY KOSMITA
   else if (rank < size * (CLEANER_PROC + RED_PROC + BLUE_PROC) / 100) {
      process_state = ALIEN_BLUE;
   }
}

int main(int argc, char **argv) {
   char processor[100];
   int provided;
   MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
   // debugging 
   /*
   {
      volatile int i = 0;
      char hostname[256];
      gethostname(hostname, sizeof(hostname));
      printf("PID %d on %s ready for attach\n", getpid(), hostname);
      fflush(stdout);
      while (0 == i)
      sleep(1);
   }
   */
   // tworzenie typu do komunikatu
   const int nitems = FIELDNO;
   int       blocklengths[FIELDNO] = {2,1,1, 1};
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
   if (process_state != CLEANER) {
      alien_procedure();
   }

   pthread_join(commThread, NULL);
   pthread_mutex_destroy(&queueMutex);
   MPI_Type_free(&MPI_PAKIET_T);
   MPI_Finalize();

   return 0;
}
