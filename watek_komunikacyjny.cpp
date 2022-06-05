#include <vector>
#include "main.hpp"
#include "watek_komunikacyjny.hpp"
#include <algorithm>
#include <pthread.h>
#include <csignal>

// tablica timestampow
unsigned *timestamps;
/*
void handleTsCollision(Packet_t &pkt) {
   // przypadek szczegolny, robimy sleep na rand dlugosc i wysylamy ponownie
   // aktualizując timestampy
   debug("Wykryto identyczny timestamp od [%i], ts: %i", pkt.src, pkt.timestamp);
   usleep(rand() % 500);

   Packet_t ackPkt = Packet_t {
       .timestamp = timestamp,
       .type      = process_state,
       .index     = 0,
       .src       = rank
   };
   sendPacket(ackPkt, pkt.src, TS_UPDATE);
}
*/

// funkcja do sortowania timestampów
bool sortByTimestamp(Entry& a, Entry& b) {
   //return a.timestamp < b.timestamp;
   // TODO: ZMIENIC SPOSOB SORTOWANIA!
   if (a.timestamp == b.timestamp) {
      return a.process_index < b.process_index;
   } else {
      return a.timestamp < b.timestamp;
   }
}
// aktualizuje timestampy kolejki po uzyskaniu ACK
void updateTimestamps(Packet_t &pkt) {
   pthread_mutex_lock(&queueMutex);
   for (auto &queue: queues) {
      for (auto &i: queue) {
         if (i.process_index == pkt.src) {
            debug("Aktualizowanie kolejki dla [%d], nowy timestamp: %d, stary %d", 
                  i.process_index, pkt.timestamp, i.timestamp);
            i.timestamp = pkt.timestamp;
            // TODO: zmienic
            std::sort(queue.begin(), queue.end(), sortByTimestamp);
         }
      }
   }
   pthread_mutex_unlock(&queueMutex);
   timestamps[pkt.src] = pkt.timestamp;
} 
// funkcja do otrzymywania requestów
void recvRequest(Packet_t &pkt) {
   pthread_mutex_lock(&queueMutex);
   std::vector<Entry> &queue = queues[pkt.index];
   debug("Odpowiadam na request od: [%i] o ts: %i", pkt.src, pkt.timestamp);
   timestamp = std::max(timestamp, pkt.timestamp) + 1;
   Entry entry = Entry {
         .timestamp     = pkt.timestamp, 
         .process_index = pkt.src, 
         .type          = pkt.type
   };
   queue.push_back(entry);
   // TODO: Zamiast sortować wstawić za ostatnim timestampem tak jak było poprzednio
   std::sort(queue.begin(), queue.end(), sortByTimestamp);
   debug("Posortowano kolejkę Requestów");
   pthread_mutex_unlock(&queueMutex);

   // Odsyłanie ACK do procesu od którego odebraliśmy REQUEST
   {
      Packet_t ackPkt = Packet_t {
          .timestamp = ++timestamp,
          .type      = process_state,
          .index     = pkt.index,
          .src       = rank
      };
      sendPacket(ackPkt, pkt.src, ACK);
   }
}

// wątek komunikacyjny; zajmuje się odbiorem i reakcją na komunikaty
void* startKomWatek(void *ptr)
{
   signal(SIGINT, NULL);
   timestamps = (unsigned*)malloc(size * sizeof(unsigned));
   MPI_Status status;
   Packet_t pkt;
   bool isFinished = false;

   // pętla główna 
   while (!isFinished) {
      //debug("Czekam na recv");
      MPI_Recv(&pkt, 1, MPI_PAKIET_T, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
      switch (status.MPI_TAG) {
         case FINISH: 
            isFinished = true;
            break;
         case ACK:
            debug("Odebrano ACK od: %i do:%i", pkt.src, rank);
            updateTimestamps(pkt);
            timestamp = std::max(timestamp, pkt.timestamp) + 1;
            pthread_mutex_lock(&acksMutex);
            acks++;
            pthread_cond_signal(&acksCond);
            pthread_mutex_unlock(&acksMutex);
            break;
         case REQUEST_P:
            recvRequest(pkt);
            break;
         case REQUEST_H: 
            recvRequest(pkt);
            break;
         // zwalnianie miejsca w kolejce (narazie we wszystkich kolejkach)
         case RELEASE:
            debug("Odebrano RELEASE od: %d do: %d, nr zasobu: %d",
                  pkt.src, rank, pkt.index);
            updateTimestamps(pkt);
            auto &queue = queues[pkt.index];
            for (auto i = queue.begin(); i < queue.end(); i++) {
               if (i->process_index == pkt.src) {
                  queue.erase(i);
                  break;
               }
            }
            break;
            /*
         case TS_UPDATE: 
            debug("Wykryto kolizję z procesem %i, timestampy: %i, %i", 
                  pkt.src, timestamp, pkt.timestamp);
            updateTimestamp(pkt);
            updateQueue(pkt);
            handleTsCollision(pkt);
            break;
            */
      }
   }
   free(timestamps);
   return NULL;
}
