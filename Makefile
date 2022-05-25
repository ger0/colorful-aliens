SOURCES=$(wildcard *.cpp)
HEADERS=$(SOURCES:.cpp=.h)
FLAGS=-DDEBUG -g -lpthread

all: main

main: $(SOURCES) $(HEADERS)
	mpic++ $(SOURCES) $(FLAGS) -o main

clear: clean

clean:
	rm main a.out

run: main
	mpirun -oversubscribe -np 8 ./main
