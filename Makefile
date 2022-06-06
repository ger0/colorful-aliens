SOURCES=$(wildcard *.cpp)
HEADERS=$(SOURCES:.cpp=.hpp)
FLAGS=-DDEBUG -g -lpthread

all: main

main: $(SOURCES) $(HEADERS)
	mpic++ $(SOURCES) $(FLAGS) -o aliens

clear: clean

clean:
	rm aliens 

run: main
	mpirun -oversubscribe -np 25 ./aliens
