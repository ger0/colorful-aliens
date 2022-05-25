#include <vector>
#include "main.h"
#include "watek_komunikacyjny.h"
#include "algorithm"

bool sortByTimestamp(Entry& a, Entry& b) {
      return a.timestamp < b.timestamp;
}

// wątek komunikacyjny; zajmuje się odbiorem i reakcją na komunikaty


void* startKomWatek(void *ptr)
{
   //MPI_Datatype MPI_PAKIET_T;
   MPI_Status status;
   bool isFinished = false;
   Packet_t pkt;
   /* Obrazuje pętlę odbierającą pakiety o różnych typach */
   while (!isFinished) {
      debug("czekam na recv");
      MPI_Recv(&pkt, 1, MPI_PAKIET_T, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
      switch (status.MPI_TAG) {
         case FINISH: 
            isFinished = true;
         break;
         case REQUEST_H: 
             //pakiet.data);
            std::vector<Entry> &queue = queues[pkt.index];
            debug("odpowiadam na requesty");
            Entry entry = Entry {
                  .timestamp     = pkt.timestamp, 
                  .process_index = pkt.src, 
                  .type          = pkt.type
                  };

            queue.push_back(entry);
            // TODO: Zamiast sortować wstawić za ostatnim timestampem tak jak było poprzednio
            std::sort(queue.begin(), queue.end(), sortByTimestamp);
            debug("Posortowano kolejkę Requestów")
         break;
      }
   }
   return NULL;
}
