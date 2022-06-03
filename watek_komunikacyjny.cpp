#include <vector>
#include "main.hpp"
#include "watek_komunikacyjny.hpp"
#include <algorithm>

// funkcja do sortowania timestampów
bool sortByTimestamp(Entry& a, Entry& b) {
      return a.timestamp < b.timestamp;
}
// aktualizuje timestampy kolejki po uzyskaniu ACK
void updateQueue(Packet_t &pkt) {
   for (auto &queue: queues) {
      for (auto &i: queue) {
         if (i.process_index == pkt.src) {
            debug("Aktualizowanie kolejki dla %d, nowy %d, stary %d", 
                  i.process_index, pkt.timestamp, i.timestamp);
            i.timestamp = pkt.timestamp;
            // TODO: zmienic
            std::sort(queue.begin(), queue.end(), sortByTimestamp);
         }
      }
   }
} // aktualizuje timestamp dla obecnego procesu
void updateTimestamp(unsigned m_ts) {
   /*
   while (entry.timestamp == timestamp) {
      // przypadek szczegolny, robimy sleep na rand dlugosc i wysylamy ponownie
      // aktualizując timestampy
      sleep(rand() % 2000);

      Packet_t ackPkt = Packet_t {
          .timestamp = timestamp,
          .type      = process_state,
          .index     = 0,
          .src       = rank
      };
      sendPacket(ackPkt, entry.process_index, 999);
   }
   */
   if (m_ts > timestamp) {
      timestamp = m_ts;
   }
   timestamp++;
}
// funkcja do otrzymywania requestów
void recvRequest(Packet_t &pkt) {
   std::vector<Entry> &queue = queues[pkt.index];
   debug("odpowiadam na request");
   Entry entry = Entry {
         .timestamp     = pkt.timestamp, 
         .process_index = pkt.src, 
         .type          = pkt.type
         };

   queue.push_back(entry);
   // TODO: Zamiast sortować wstawić za ostatnim timestampem tak jak było poprzednio
   std::sort(queue.begin(), queue.end(), sortByTimestamp);
   updateTimestamp(entry.timestamp);
   debug("Posortowano kolejkę Requestów");

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
   MPI_Status status;
   bool isFinished = false;
   Packet_t pkt;

   // pętla główna 
   while (!isFinished) {
      debug("czekam na recv");
      MPI_Recv(&pkt, 1, MPI_PAKIET_T, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
      switch (status.MPI_TAG) {
         case FINISH: 
            isFinished = true;
            break;
         case ACK:
            debug("Odebrano ACK od: %i do:%i", pkt.src, rank);
            updateTimestamp(pkt.timestamp);
            updateQueue(pkt);
            acks++;
            break;
         case REQUEST_P:
            recvRequest(pkt);
            break;
         case REQUEST_H: 
            recvRequest(pkt);
            break;
      }
   }
   return NULL;
}
