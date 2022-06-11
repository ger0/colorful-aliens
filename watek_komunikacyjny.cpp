#include <vector>
#include "main.hpp"
#include "watek_komunikacyjny.hpp"
#include <algorithm>
#include <pthread.h>
#include <csignal>

// funkcja do otrzymywania requestów
void recvRequest(Packet_t& pkt) {
   debug("Odpowiadam na request od: [%i] o ts: %i", pkt.src, pkt.timestamp);
   Entry entry = Entry {
         .timestamp     = pkt.timestamp, 
         .process_index = pkt.src, 
         .type          = pkt.type
   };
   updateTimestamps(pkt.timestamp, pkt.src);
   addEntry(entry, pkt.index);
   // Odsyłanie ACK do procesu od którego odebraliśmy REQUEST
   Packet_t ackPkt = prepareACK(pkt.index);
   sendPacket(ackPkt, pkt.src, ACK);
}

// funkcja do otrzymywania ACK
void recvAck(Packet_t& pkt) {
   debug("Odebrano ACK od: [%i]", pkt.src);
   updateTimestamps(pkt.timestamp, pkt.src);
   incrAcks();
}

// funkcja do otrzymywania releaseów i zwalniania zarezerw. miejsca w kolejce
void recvRelease(Packet_t& pkt) {
   debug("Odebrano RELEASE od: [%d], nr zasobu: %d",
         pkt.src, pkt.index);
   updateTimestamps(pkt.timestamp, pkt.src);
   rmEntry(pkt.index, pkt.src);
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
         case REQUEST_G:
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
