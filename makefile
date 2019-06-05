#makefile

CC:=gcc
#CFLAGS:=

SRC:=sipc.c

lib:
	$(CC) -g -Wall -fPIC -c $(SRC) -lnanomsg -lpthread
	$(CC) -g -shared -Wl,-soname,libsipc.so.1 -o libsipc.so.1.0  sipc.o
	ln -sf libsipc.so.1.0 libsipc.so.1
	ln -sf libsipc.so.1.0 libsipc.so

test:
	gcc -g test_sipc.c  -L . -lsipc -lnanomsg -o test_sipc 

all: lib test

clean:
	rm -rf *.o
	rm libsipc.so libsipc.so.1 libsipc.so.1.0 
	rm test_sipc	

.PHONY: all clean

