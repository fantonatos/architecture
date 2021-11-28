EXE = cache-sim
SOURCE = cache-sim.c
CFLAGS = -std=c99 -Ofast
LIBS = -lpthread
CC = gcc

$(EXE): $(SOURCE)
	$(CC) -o $@ $< $(CFLAGS) $(LIBS)
	strip $@

clean:
	rm -f $(EXE) *.o
