#include "main.h"
#include "watek_komunikacyjny.h"

/* wątek komunikacyjny; zajmuje się odbiorem i reakcją na komunikaty */
void* startKomWatek(void *ptr)
{

   MPI_Datatype MPI_PAKIET_T;
    MPI_Status status;
    bool isFinished = false;
    bool is_message = false;
    Packet_t pakiet;
    /* Obrazuje pętlę odbierającą pakiety o różnych typach */
    while (!isFinished) {
	debug("czekam na recv");
        MPI_Recv( &pakiet, 1, MPI_PAKIET_T, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
      switch ( status.MPI_TAG ) {
      case FINISH: 
         isFinished = true;
      break;
      case REQUEST_H: 
             debug("Dostałem wiadomość od %d z danymi %d",pakiet.src, pakiet.data);
      break;
      case REQUEST_P: 
             debug("Dostałem wiadomość od %d z danymi %d",pakiet.src, pakiet.data);
      break;
      case ACK: 
             debug("Dostałem wiadomość od %d z danymi %d",pakiet.src, pakiet.data);
      break;
      default:
      break;
      }
   }
   return NULL;
}
