#include <cstdio>
#include <cstdlib>
#include <mpi.h>
#include <ctime>
#include <unistd.h>
#include <cstdlib>
#include <pthread.h>
#include <csignal>
#include <cassert>

#include <vector>
#include <queue>

#include "main.hpp"
#include "watek_komunikacyjny.hpp"

bool isFinished = false;

Type process_state;

// tablica kolejek do poszczególnych zasobów
std::vector<std::vector<Entry>> queues = std::vector<std::vector<Entry>>(
      HOTEL_COUNT + GUIDE_COUNT
);
pthread_mutex_t   queueMutex  = PTHREAD_MUTEX_INITIALIZER;

// wątek komunikacyjny
pthread_t         commThread;

// zmienna zliczajaca ilosc odebranych ACK
unsigned          acks = 0;
pthread_mutex_t   acksMutex   = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t    acksCond    = PTHREAD_COND_INITIALIZER;

// zmienne okreslajace proces
int               size, rank, len;
unsigned          timestamp = 0;
MPI_Datatype      MPI_PAKIET_T;
//
// tablica z ostatnimi otrzymanymi timestampami poszczególnych procesów 
unsigned* initTimestampsArray();
unsigned* timestamps = initTimestampsArray();
pthread_mutex_t   timestampsMutex = PTHREAD_MUTEX_INITIALIZER;

// wersja tymczasowa
unsigned chooseResource(unsigned offset) {
   // posortowane rozmiarami indeksy
   auto cmp = [](int left, int right) {
      return queues[left].size() >= queues[right].size();
   };
   std::priority_queue<int, std::vector<int>, decltype(cmp)> order(cmp);

   /* typ zasobu - hotel lub przewodnik */
   // HOTEL
   if (offset == HOTEL_OFFSET) {
      for (unsigned id = 0; id < HOTEL_COUNT; id++) {
         auto &queue = queues[id];
         size_t length = queue.size();
         if (length == 0) {
            return id;
         } else {
            order.push(id);
         }
      }
      /* sprawdzenie koloru - wybieramy kolejke ktora ma najmniejsza liczbe elementow
      *  o kolorach obecnego procesu, jeżeli takiej kolejki nie ma - 
      *  wybieramy kolejkę o najmniejszej liczbie wpisów */
      int front = order.top();
      for (; !order.empty(); order.pop()) {
         auto& it = order.top();
         for (auto& entry: queues[it]) {
            if (entry.type != process_state) {
               break;
            }
            if (&entry == &queues[it].back()) {
               return it;
            }
         }
      }
      return front;
   // PRZEWODNICY
   } else {
   }
   return 0;
}

void sendPacket(Packet_t &pkt, int destination, int tag) {
    MPI_Send(&pkt, 1, MPI_PAKIET_T, destination, tag, MPI_COMM_WORLD);
}

void funcINT() {
   debug("------------------ Zamykanie programu... ------------------");
   isFinished = true;
   Packet_t pkt;
   sendPacket(pkt, rank, FINISH);
}

Packet_t prepareRequest(int index) {
   return Packet_t {
      .timestamp  = timestamp, 
      .type       = process_state,
      .index      = index,
      .src        = rank
   };
}

void sendRelease(int resourceID) {
   Packet_t rel_packet = Packet_t{
      .timestamp  = timestamp,
      .type       = process_state,
      .index      = resourceID,
      .src        = rank
   };
   acks = 0;
   timestamp++;
   for (unsigned i = 0; i < size; i++) {
      sendPacket(rel_packet, i, RELEASE);
   }
   // TODO: odebrać ACK?
}

bool checkAcks(unsigned req_timestamp) {
   for (unsigned i = 0; i < size; i++) {
      if (req_timestamp >= timestamps[i]) {
         debug("Anulowanie rezerwacji, bo [%d] stampy: %d >= %d", 
               i, req_timestamp, timestamps[i]);
         return false;
      }
   }
   return true;
}

// procedura dla kosmitow
void alien_procedure() {
   while (!isFinished) {
      //int hotelID = 0;
      int hotelID = chooseResource(HOTEL_OFFSET);
      debug("Proces o kolorze: %d wybrał hotel %d", (int)process_state, hotelID);
      // spimy randomowa dlugosc 
      usleep(rand() % 500);
      // requestujemy miejsce w kolejce do wszystkich procesow
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
      auto& queue = queues[hotelID];
      /* sprawdzenie czy wszystkie ACK maja wiekszy timestamp niz przy wysylaniu 
       * na obecną chwilę gdy timestamp */
      if (checkAcks(req_packet.timestamp)) {
         bool isDifferentColour = false;
         // debug print kolejki
         {
            debug("  Kolejka dostepu do hotelu %d:", hotelID);
            for (unsigned i = 0; i < queue.size(); i++) {
               debug("     [%d], idx: %d, kolor: %d, timestamp: %d", 
                     queue[i].process_index, i, 
                     (int)queue[i].type, queue[i].timestamp
               );
            }
         }
         /* Sprawdzenie czy w kolejce do hotelu nie ma innego koloru przed obecnym procesem
          * jeżeli nie - proces wchodzi do hotelu */
         for (unsigned i = 0; i < queue.size() && i < SLOTS_PER_HOTEL; i++) {
            if (queue[i].process_index == rank && !isDifferentColour) {
               debug("===== Proces %d wchodzi do hotelu %d o kolorze: %d =====", 
                     rank, hotelID, (int)process_state);      
               break;
            } else if (queue[i].type != process_state) {
               debug("Wykryto kosmitę o innym kolorze!");
               isDifferentColour = true; 
               break;
            }
         }
         // Opuszczanie miejsca w kolejce gdy wykryto inny kolor 
         if (isDifferentColour) {
            //
         }
      }
      sendRelease(hotelID);
      pthread_mutex_unlock(&acksMutex);
      pthread_mutex_unlock(&queueMutex);

   }
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

// inicjalizowanie tablicy indeksow 
unsigned* initTimestampsArray() {
   unsigned* point = (unsigned*)malloc(size * sizeof(unsigned));
   assert(point != NULL);
   for (unsigned i = 0; i < size; i++) {
      timestamps[i] = 0;
   }
   return point;
}

int main(int argc, char **argv) {
   signal(SIGINT, (__sighandler_t)&funcINT);
   char processor[100];
   int provided;
   MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
   
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
   } else {
   }
   pthread_join(commThread, NULL);
   pthread_mutex_destroy(&queueMutex);
   MPI_Type_free(&MPI_PAKIET_T);
   MPI_Finalize();
   free(timestamps);

   return 0;
}
