all:
	make server peer
	
server: server.c
	gcc server.c -pthread -o server

peer: peer.c
	gcc peer.c -pthread -lm -o peer
	
clean:
	rm -f server peer
