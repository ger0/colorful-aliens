#pragma once

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <vector>

// Typy wiadomości
#define FINISH    1
#define REQUEST_P 2
#define REQUEST_H 3
#define ACK       4
#define RELEASE   5

// Ilosc zasobow
#define HOTEL_COUNT     10
#define GUIDE_COUNT     4

#define HOTEL_OFFSET    0
#define GUIDE_OFFSET    HOTEL_COUNT

#define SLOTS_PER_HOTEL 5
#define SLOTS_PER_CLEAN 1

// Procentowa ilosc procesow 
#define CLEANER_PROC    20
#define RED_PROC        40
#define BLUE_PROC       40

enum Type {
   CLEANER     = 0,
   ALIEN_RED   =  1,
   ALIEN_BLUE  = 2,
};

// Pakiet do wysylania wiadomosci
struct Hotel {
   int   taken = 0;
   int   slots = SLOTS_PER_HOTEL;
   Type  colour;
};

#define FIELDNO 4 // liczba pól w Packet_t
struct Packet_t {
    unsigned   timestamp;  // zegar lamporta
    Type       type;       // sprzatacz lub kolor kosmity [0..2]
    int        index;      // nr zasobu o ktory sie ubiegamy
    int        src;        // źródło wiadomosci
};
// Kolejka
struct Entry {
   unsigned timestamp;
   int      process_index;
   Type     type;
};

extern MPI_Datatype MPI_PAKIET_T;
extern int  rank, size;
extern Type process_state;

extern pthread_mutex_t queueMutex;

// liczba odpowiedzi uzyskanych dla requesta
extern unsigned acks;
extern pthread_mutex_t acksMutex;
extern pthread_cond_t  acksCond;

extern std::vector<std::vector<Entry>> queues;
extern unsigned timestamp;
extern unsigned *timestamps;
extern pthread_mutex_t timestampsMutex;

// Funkcja do wysylania wiadomosci
void sendPacket(Packet_t &pkt, int destination, int tag);

#ifdef DEBUG
#define debug(FORMAT,...) printf("%c[%d;%dm [%d]: " FORMAT "%c[%d;%dm\n",  27, (1+(rank/7))%2, 31+(6+rank)%7, rank, ##__VA_ARGS__, 27,0,37);
#else
#define debug(...) ;
#endif

#define P_WHITE printf("%c[%d;%dm",27,1,37);
#define P_BLACK printf("%c[%d;%dm",27,1,30);
#define P_RED printf("%c[%d;%dm",27,1,31);
#define P_GREEN printf("%c[%d;%dm",27,1,33);
#define P_BLUE printf("%c[%d;%dm",27,1,34);
#define P_MAGENTA printf("%c[%d;%dm",27,1,35);
#define P_CYAN printf("%c[%d;%d;%dm",27,1,36);
#define P_SET(X) printf("%c[%d;%dm",27,1,31+(6+X)%7);
#define P_CLR printf("%c[%d;%dm",27,0,37);

#define println(FORMAT, ...) printf("%c[%d;%dm [%d]: " FORMAT "%c[%d;%dm\n",  27, (1+(rank/7))%2, 31+(6+rank)%7, rank, ##__VA_ARGS__, 27,0,37);
