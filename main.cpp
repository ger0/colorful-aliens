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

// debugging 
int currentlyIn   = -9999;
int currentGuide  = -9999;

// tablica kolejek do poszczególnych zasobów
std::vector<std::vector<Entry>> queues = std::vector<std::vector<Entry>>(
      HOTEL_COUNT + GUIDE_COUNT
);
pthread_mutex_t   queueMutex  = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t    queueCond   = PTHREAD_COND_INITIALIZER;

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
         auto& queue = queues[id];
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
      unsigned bestId = GUIDE_OFFSET;

      for (unsigned id = GUIDE_OFFSET; id < GUIDE_COUNT + GUIDE_OFFSET; id++) {
         auto& queue = queues[id];
         size_t length = queue.size();
         if (length == 0) {
            return id;
         } else if (length < queues[bestId].size()) {
            bestId = id;
         }
      }
      return bestId;
   }
}

// Wrapper na wysyłanie pakietów ------------------------------------------
void sendPacket(Packet_t& pkt, int destination, int tag) {
    MPI_Send(&pkt, 1, MPI_PAKIET_T, destination, tag, MPI_COMM_WORLD);
}

void broadcastPacket(Packet_t& pkt, int tag) {
   for (unsigned i = 0; i < size; i++)    sendPacket(pkt, i, tag);
}

// wysyla do wszystkich i czeka na odbior ACK
void broadcastAndAcks(Packet_t& pkt, int tag) { 
   pthread_mutex_lock(&acksMutex);
   acks = 0;
   broadcastPacket(pkt, tag);
   /* Odbiór ACK i sortowanie kolejki w wątku komunikacyjnym
   *  kontynuacja kiedy otrzymamy ACK od kazdego procesu jak odpowiedzą */
   while (acks < size) {
      pthread_cond_wait(&acksCond, &acksMutex);
   }
   pthread_mutex_unlock(&acksMutex);
}
// ------------------------------------------------------------------------
Packet_t prepareRequest(int index) {
   return Packet_t {
      .timestamp  = timestamp, 
      .type       = process_state,
      .index      = index,
      .src        = rank
   };
}

void broadcastRelease(int resourceID) {
   Packet_t rel_packet = Packet_t{
      .timestamp  = timestamp,
      .type       = process_state,
      .index      = resourceID,
      .src        = rank
   };

   pthread_mutex_lock(&timestampsMutex);
   ++timestamp;
   pthread_mutex_unlock(&timestampsMutex);

   broadcastPacket(rel_packet, RELEASE);
   // TODO: odebrać ACK?
}

// Funkcja wykonywania po uzyskaniu SIGINT
void funcINT() {
   debug("------------------ Zamykanie programu... ------------------");
   isFinished = true;
   Packet_t pkt;
   sendPacket(pkt, rank, FINISH);
}
// debug print kolejki
void debugPrintQueue(unsigned hotelID) {
   auto& queue = queues[hotelID];
   debug("  Kolejka dostepu do hotelu %d:", hotelID);
   for (unsigned i = 0; i < queue.size(); i++) {
      debug("     [%d], idx: %d, kolor: %d, timestamp: %d", 
            queue[i].process_index, i, 
            (int)queue[i].type, queue[i].timestamp
      );
   }
}

// sprawdzenie czy wszystkie procesy maja timestamp wiekszy niz requesta
bool checkAcks(unsigned req_timestamp) {
   pthread_mutex_lock(&timestampsMutex);
   for (unsigned i = 0; i < size; i++) {
      if (req_timestamp >= timestamps[i]) {
         debug("Anulowanie rezerwacji, bo [%d] stampy: %d >= %d", 
               i, req_timestamp, timestamps[i]);
         pthread_mutex_unlock(&timestampsMutex);
         return false;
      }
   }
   pthread_mutex_unlock(&timestampsMutex);
   return true;
}

// zwraca true jeżeli obecny proces jest na szczycie kolejki żądań
bool procAtQueueTop(std::vector<Entry>& queue) {
   return queue.front().process_index == rank;
}

// procedura wejścia do sekcji krytycznej dla zasobu - Przewodnika
void getGuide() {
   pthread_mutex_lock(&queueMutex);
   int guideID = chooseResource(GUIDE_OFFSET);
   pthread_mutex_unlock(&queueMutex);
   
   debug("Proces o kolorze: %d wybrał przewodnika %d", (int)process_state, guideID);

   pthread_mutex_lock(&timestampsMutex);
   ++timestamp;
   pthread_mutex_unlock(&timestampsMutex);

   Packet_t req_packet = prepareRequest(guideID);
   broadcastAndAcks(req_packet, REQUEST_G);

   if (checkAcks(req_packet.timestamp)) {
      pthread_mutex_lock(&queueMutex);
      /* jezeli proces nie jest na szczycie kolejki zadan, to czekamy na otrzymanie jakiegos release 
      *  po czym sprawdzamy czy juz znajduje sie na szczycie */
      while(!procAtQueueTop(queues[guideID])) {
         pthread_cond_wait(&queueCond, &queueMutex);
      }
      pthread_mutex_unlock(&queueMutex);

      currentGuide = guideID;
      debug("Proces o kolorze: %d zabrał przewodnika %d", (int)process_state, guideID);
      usleep(rand() % 10'000 + 2'000);
   }
   currentGuide = -99999;
   broadcastRelease(guideID);
}

// ----------------------------------- główna pętla dla kosmitow ------------------------------------
void alienProcedure() {
   while (!isFinished) {
      //int hotelID = 0;
      // spimy randomowa dlugosc 
      usleep(rand() % 2'000);

      pthread_mutex_lock(&queueMutex);
      int hotelID = chooseResource(HOTEL_OFFSET);
      pthread_mutex_unlock(&queueMutex);

      debug("Proces o kolorze: %d wybrał hotel %d", (int)process_state, hotelID);
      // requestujemy miejsce w kolejce do wszystkich procesow
      pthread_mutex_lock(&timestampsMutex);
      ++timestamp;
      pthread_mutex_unlock(&timestampsMutex);

      Packet_t req_packet = prepareRequest(hotelID);
      broadcastAndAcks(req_packet, REQUEST_H);

      pthread_mutex_lock(&queueMutex);
      auto& queue = queues[hotelID];

      if (checkAcks(req_packet.timestamp)) {
         debugPrintQueue(hotelID);
         /* Sprawdzenie czy w kolejce do hotelu nie ma innego koloru przed obecnym procesem
          * jeżeli nie - proces wchodzi do hotelu */
         bool isDifferentColour = false;
         for (unsigned i = 0; i < queue.size() && i < SLOTS_PER_HOTEL; i++) {
            if (queue[i].process_index == rank && !isDifferentColour) {
               currentlyIn = hotelID;
               debug("===== Proces %d wchodzi do hotelu %d o kolorze: %d =====", 
                     rank, hotelID, (int)process_state);      

               pthread_mutex_unlock(&queueMutex);

               getGuide();

               break;
            } else if (queue[i].type != process_state) {
               debug("Wykryto kosmitę o innym kolorze!");
               isDifferentColour = true; 
               break;
            }
         }
         // Opuszczanie miejsca w kolejce gdy wykryto inny kolor 
         if (isDifferentColour) {
         }
      }
      pthread_mutex_unlock(&queueMutex);
      currentlyIn = -99999;
      broadcastRelease(hotelID);
   }
}

// ----------------------------------- główna pętla dla sprzątaczy ------------------------------------
void cleanerProcedure() {
   while (!isFinished) {
      unsigned hotelID;

      pthread_mutex_lock(&queueMutex);
      std::vector<unsigned> availIDs;
      for (unsigned i = 0; i < HOTEL_COUNT; i++) {
         for (auto& entry: queues[i]) {
            if (entry.type == CLEANER) {
               break;
            }
            if (&entry == &queues[i].back()) {
               availIDs.push_back(i);
            }
         }
      }
      pthread_mutex_unlock(&queueMutex);

      if (availIDs.size() != 0) {
         hotelID = rand() % availIDs.size(); 
      } else {
         // technicznie ten sleep nie jest potrzebny
         /* TODO: dorobic sprawdzenie czy procesy nie przekroczyly progu wejscia do hoteli 
         * wtedy probowac dostac sie do kolejki do hotelu */
         usleep(rand() % 20'000);
         continue;
      }

      Packet_t req_packet = prepareRequest(hotelID);
      broadcastAndAcks(req_packet, REQUEST_H);

      if (checkAcks(req_packet.timestamp)) {
         pthread_mutex_lock(&queueMutex);
         /* jezeli proces nie jest na szczycie kolejki zadan, to czekamy na otrzymanie jakiegos release 
         *  po czym sprawdzamy czy juz znajduje sie na szczycie */
         while(!procAtQueueTop(queues[hotelID])) {
            pthread_cond_wait(&queueCond, &queueMutex);
         }
         pthread_mutex_unlock(&queueMutex);

         currentlyIn = hotelID;
      }
      debug("CZYSZCZENIE HOTELU %d", hotelID);
      usleep(rand() % 10'000 + 2'000);
      broadcastRelease(hotelID);
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

// funkcja przypisująca odpowiedni stan procesowi (na podstawie rank)
void assignState() {
   // SPRZATACZ 
   if (rank < size * CLEANER_PROC / 100) {
      process_state = CLEANER;
   } 
   // FIOLETOWY KOSMITA
   else if (rank < size * (CLEANER_PROC + RED_PROC) / 100) {
      process_state = ALIEN_RED;
   } 
   // BLEKITNY KOSMITA
   else if (rank < size * (CLEANER_PROC + RED_PROC + BLUE_PROC) / 100) {
      process_state = ALIEN_BLUE;
   }
}

int main(int argc, char **argv) {
   signal(SIGINT, (__sighandler_t)&funcINT);
   char processor[100];
   int provided;
   MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
   
   // tworzenie typu do komunikatu
   const int nitems = FIELDNO;
   int       blocklengths[FIELDNO] = {2, 1, 1, 1};
   MPI_Datatype typy[FIELDNO] = {MPI_UNSIGNED, MPI_INT, MPI_INT, MPI_INT};

   MPI_Aint     offsets[FIELDNO]; 
   offsets[0] = offsetof(Packet_t, timestamp);
   offsets[1] = offsetof(Packet_t, type);
   offsets[2] = offsetof(Packet_t, index);
   offsets[3] = offsetof(Packet_t, src);

   MPI_Type_create_struct(nitems, blocklengths, offsets, typy, &MPI_PAKIET_T);
   MPI_Type_commit(&MPI_PAKIET_T);

   MPI_Comm_size(MPI_COMM_WORLD, &size);
   MPI_Comm_rank(MPI_COMM_WORLD, &rank);
   MPI_Get_processor_name(processor, &len);

   srand(time(NULL) + rank);

   pthread_create(&commThread, NULL, startKomWatek, 0);
   assignState();
   // KOSMITA
   if (process_state != CLEANER) {
      alienProcedure();
   // sprzątacz 
   } else {
      cleanerProcedure();  
   }
   pthread_join(commThread, NULL);

   /* ------ na obecną chwilę to się prawdopodbnie nie wykona ------ */
   pthread_mutex_destroy(&queueMutex);
   pthread_mutex_destroy(&timestampsMutex);
   pthread_mutex_destroy(&acksMutex);

   pthread_cond_destroy(&acksCond);
   pthread_cond_destroy(&queueCond);

   MPI_Type_free(&MPI_PAKIET_T);
   MPI_Finalize();

   free(timestamps);

   return 0;
}
