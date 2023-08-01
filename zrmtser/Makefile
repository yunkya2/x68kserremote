CFLAGS = -I. -I../include -O

all: zrmtser

zrmtser: zrmtser.o
	$(CC) -o $@ $^ -liconv

zrmtser.o: ../include/x68kremote.h

clean:
	-rm -f *.o *.exe
