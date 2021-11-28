EXE = predictors
SOURCE = predictors.c
OBJ := $(SOURCE:%.c=%.o)
CFLAGS = -Wall -g -Ofast
LIB = -lpthread

$(EXE): $(OBJ)
	cc -o $@ $(LIB) $(OBJ)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(EXE) *.o