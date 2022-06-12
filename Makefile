SOURCES=$(wildcard *.cpp)
HEADERS=$(SOURCES:.cpp=.hpp)
FLAGS= -g -lpthread

debug: DEBUG = -DDEBUG
debug: main

all: main

main: $(SOURCES) $(HEADERS)
	mpic++ $(DEBUG) $(SOURCES) $(FLAGS) -o aliens

clear: clean

clean:
	rm aliens 

run: main
	mpirun -oversubscribe -np 25 ./aliens
