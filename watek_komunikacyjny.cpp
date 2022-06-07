#include <vector>
#include "main.hpp"
#include "watek_komunikacyjny.hpp"
#include <algorithm>
#include <pthread.h>
#include <csignal>

// funkcja do sortowania timestampów
bool sortByTimestamp(Entry& a, Entry& b) {
   if (a.timestamp == b.timestamp) {
      return a.process_index < b.process_index;
   } else {
      return a.timestamp < b.timestamp;
   }
}
// aktualizuje timestampy kolejki po uzyskaniu ACK
void updateTimestamps(Packet_t &pkt) {
   pthread_mutex_lock(&timestampsMutex);
   timestamps[pkt.src] = pkt.timestamp;
   timestamp = std::max(timestamp, pkt.timestamp) + 1;
   pthread_mutex_unlock(&timestampsMutex);
} 
// funkcja do otrzymywania requestów
void recvRequest(Packet_t& pkt) {
   pthread_mutex_lock(&queueMutex);
   std::vector<Entry> &queue = queues[pkt.index];
   debug("Odpowiadam na request od: [%i] o ts: %i", pkt.src, pkt.timestamp);
   updateTimestamps(pkt);
   Entry entry = Entry {
         .timestamp     = pkt.timestamp, 
         .process_index = pkt.src, 
         .type          = pkt.type
   };
   queue.push_back(entry);
   // TODO: Zamiast sortować wstawić za ostatnim timestampem tak jak było poprzednio
   std::sort(queue.begin(), queue.end(), sortByTimestamp);
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
// funkcja do otrzymywania ACK
void recvAck(Packet_t& pkt) {
   updateTimestamps(pkt);
   pthread_mutex_lock(&acksMutex);
   acks++;
   debug("Odebrano ACK od: [%i], łącznie otrzymano %d ACK", pkt.src, acks);
   pthread_cond_signal(&acksCond);
   pthread_mutex_unlock(&acksMutex);
}
// funkcja do otrzymywania releaseów i zwalniania zarezerw. miejsca w kolejce
void recvRelease(Packet_t& pkt) {
   debug("Odebrano RELEASE od: [%d], nr zasobu: %d",
         pkt.src, pkt.index);
   updateTimestamps(pkt);
   auto &queue = queues[pkt.index];
   for (auto i = queue.begin(); i < queue.end(); i++) {
      if (i->process_index == pkt.src) {
         queue.erase(i);
         break;
      }
   }
}
// wątek komunikacyjny; zajmuje się odbiorem i reakcją na komunikaty
void* startKomWatek(void *ptr)
{
   signal(SIGINT, NULL);
   MPI_Status status;
   Packet_t pkt;
   bool isFinished = false;

   // pętla główna wątku
   while (!isFinished) {
      //debug("Czekam na recv");
      MPI_Recv(&pkt, 1, MPI_PAKIET_T, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
      switch (status.MPI_TAG) {
         case FINISH: 
            isFinished = true;
            break;
         case ACK:
            recvAck(pkt);
            break;
         case REQUEST_P:
            recvRequest(pkt);
            break;
         case REQUEST_H: 
            recvRequest(pkt);
            break;
         case RELEASE:
            recvRelease(pkt);
            break;
      }
   }
   return NULL;
}
