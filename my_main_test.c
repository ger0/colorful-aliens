
#include <stdio.h>
#include <stdlib.h>

#define FIELDNO 3
typedef struct {
    int ts;       /* timestamp (zegar lamporta */
    int type;      /* kosmita fioletowy, błękitny lub sprzątacz [0..2]*/
    int index;     /* nr hotelu lub przewodnika, w zaleznosci od tego gdzie znajduje sie proces i jakiego jest typu */
    int src;       /* źródło */
} packet_t;
MPI_Datatype MPI_PAKIET_T;
#define GUIDE 4
#define HOTEL 10
int main() {
    // state_t stan;
    MPI_Type_commit(&MPI_PAKIET_T);

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);


    int rank;
    int size;
    int *hotels = (int*)malloc(10 * sizeof(int));
    for (int i = 0; i < HOTEL; i++) {
        printf("petla %d\n", i);
    }
    int *guides = (int*)malloc(10 * sizeof(int));

    srand(rank);

    if (rank < size*0.2){
        //TODO: cleaner
    }
    if (rank >= size*0.2 and rank%2 == 0){
        //TODO: blue
        //rand wybierz hotel
        //wyslij req_h
        //odbierz ack od wszystkich
        //wejdz do hotelu jezeli mozesz i wyslij req_p
        //wycieczka, a potem zwolnienie zasobów
    }
    if (rank >= size*0.2 and rank%2 != 0){
        //TODO: purple
    }

    return 0;
}