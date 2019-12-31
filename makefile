#makefile

CC:=gcc
#CFLAGS:=

SRC:=sipc.c

lib:
	$(CC) -g -Wall -fPIC -I/usr/include/glib-2.0/ -I/usr/lib/x86_64-linux-gnu/glib-2.0/include/ -lpthread -lglib-2.0 -lnanomsg -c $(SRC)
	$(CC) -g -shared -Wl,-soname,libsipc.so.1 -o libsipc.so.1.0  sipc.o
	ln -sf libsipc.so.1.0 libsipc.so.1
	ln -sf libsipc.so.1.0 libsipc.so

test:
	gcc -g test_sipc.c -o test_sipc -lsipc -L /usr/lib/x86_64-linux-gnu -L /home/sunil/Downloads/nanomsg-master/build -L /home/sunil/Downloads/sipc-master -lpthread -lglib-2.0 -lnanomsg

all: lib test

clean:
	rm -rf *.o
	rm libsipc.so libsipc.so.1 libsipc.so.1.0 
	rm test_sipc	

.PHONY: all clean

