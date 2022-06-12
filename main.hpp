#pragma once

#include <cstdint>
#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <cinttypes>

// id procesu
extern int rank;

// typ procesu 
enum procType {
    CLEANER     =  0,
    ALIEN_RED   =  1,
    ALIEN_BLUE  =  2,
};

// typy wiadomości
enum msgType {
    FINISH = 0,
    REQUEST_G,
    REQUEST_H,
    ACK,
    RELEASE   
};

// pakiet danych wysyłanych w komunikacie
struct Packet_t {
    uint64_t   timestamp;  // zegar lamporta
    procType   type;       // sprzatacz lub kolor kosmity [0..2]
    int        index;      // nr zasobu o ktory sie ubiegamy
    int        src;        // źródło wiadomosci
};

// wpis w kolejce
struct Entry {
    uint64_t    timestamp;
    int         process_index;
    procType    type;
};

// typ do wysylania pakietow w komunikacie
extern MPI_Datatype MPI_PAKIET_T;

uint64_t getTimestamp(bool update = true);         // zwraca timestamp po czym go aktualizuje
unsigned incrAcks();                               // inkrementuje liczbę otrzymanych ACK
void addEntry(Entry& entry, int resId);            // dodaje wpis do kolejki zasobów
void rmEntry(int resId, int procIndex);            // usuwa wpis z kolejki do zasobów
void updateTimestamps(uint64_t ts, int procIndex); // aktualizuje timestampy

// funkcje preparujące pakiet
Packet_t prepareACK(int index);
Packet_t prepareRequest(int index);

// Funkcja do wysylania wiadomosci
void sendPacket(Packet_t &pkt, int destination, int tag);

#ifdef DEBUG
#define debug(FORMAT,...) printf("%c[%d;%dm [%d] - (%d): " FORMAT "%c[%d;%dm\n",  27, (1+(rank/7))%2, 31+(6+rank)%7, rank, getTimestamp(false), ##__VA_ARGS__, 27,0,37);
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
