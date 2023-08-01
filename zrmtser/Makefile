CFLAGS = -O

all: zrmtser

zrmtser: zrmtser.o
	$(CC) -o $@ $^ -liconv

clean:
	-rm -f *.o *.exe
