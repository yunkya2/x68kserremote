GIT_REPO_VERSION=$(shell git describe --tags --always)

CFLAGS = -I. -I../include -O -DGIT_REPO_VERSION=\"$(GIT_REPO_VERSION)\"

all: zrmtser

zrmtser: zrmtser.o
	$(CC) -o $@ $^ -liconv

zrmtser.o: ../include/x68kremote.h

clean:
	-rm -f *.o *.exe
