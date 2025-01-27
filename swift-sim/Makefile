SOURCE = $(wildcard *.cpp)
TARGETS = $(patsubst %.cpp, %, $(SOURCE))
 
CC = g++
CFLAGS = -c -Wall -O3 -g -I.  
  
all: compile link
	

compile: $(TARGETS)	

$(TARGETS):%:%.cpp
	$(CC) $< $(CFLAGS) -o $@.o 

link: $(TARGETS)
	g++ main.o -lpthread trace_reader.o cache.o gpu.o kernel.o mem_fetch.o crossbar.o config_reader.o dram.o -o swiftgpu-origin

.PHONY:clean all

clean:
	-rm -rf *.o  swiftsim-origin
