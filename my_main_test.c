// #ifndef GLOBALH
// #define GLOBALH

#define _GNU_SOURCE
// #include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#define HOTEL 10

// state_t stan;
int rank;
int size;
int hotels = malloc(sizeof(int)*10);

for (int i = 0; i < sizeof(hotels); i++) 
    printf("petla %d\n", i);