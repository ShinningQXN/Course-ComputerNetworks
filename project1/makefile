CC	 	= gcc
LD	 	= gcc
CFLAGS	 	= -Wall -O2 #-g

LDFLAGS	 	= 
DEFS 	 	=

all:	server client test

server: server.c
	$(CC) $(DEFS) $(CFLAGS) $(LIB) -o server server.c

client: client.c
	$(CC) $(DEFS) $(CFLAGS) $(LIB) -o client client.c

test:	test.cpp
	$(CC) $(DEFS) $(CFLAGS) $(LIB) -lstdc++ -o test test.cpp

clean:
	rm -f *.o
	rm -f *~
	rm -f core.*.*
	rm -f server
	rm -f client
	rm -f test