SOURCES=$(wildcard *.cpp)
HEADERS=$(SOURCES:.cpp=.hpp)
FLAGS=-DDEBUG -g -lpthread

all: main

main: $(SOURCES) $(HEADERS)
	mpic++ $(SOURCES) $(FLAGS) -o aliens

clear: clean

clean:
	rm main a.out

run: main
	mpirun -oversubscribe -np 20 ./aliens
