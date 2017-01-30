# Napster-Peer2Peer
Napster is the Peer to Peer file transfer.
*****************************************************************************************************************
[README]

This application is Peer to Peer applicantion build on the architecture of Napster. Below are the details
how to use this application.

Consist of two files.
	1. Peer.c
	2. Server.c
	3. Makefile

[How to run?]

Step 1: Running Server
	1. Open terminal
	2. exec 'make server'
	3. ./server [ip] [port]
		[ip] : (optional) Ip address on which you want to run your server eg. 10.1.227.154.
		This param is optional if not mentioned the server will on 12.0.0.1.

		[port]: (optional) Port on which you want to run your server eg. 10.1.227.154.
		If not mentioned then it will take any random available port.

Working: Each server will automatically  create the folder name "p2p-server"  

Step 2: Running Peer
	1. Open terminal
	2. exec "make peer"
	3. ./peer [server ip] [server ip] [own ip]
		[server ip] : (required) Ip address of central server.

		[server port]: (required) Port of central server.

		[ip] : (optional) Ip address on which you want to run your server eg. 10.1.227.154.
		This param is optional if not mentioned the server will on 12.0.0.1.
	
Working: Each peer will automatically create the folder name "p2p-file".

How run multiple peer on same machine?
	1. Suppose we want 4 peer to run on one machine. Then create 4 sperate folders 
	lets call them peer1, peer2, peer3 and peer4.
	
	2. Copy "make file" and "Peer.c" file on EACH of these folders.
	3. Do the same step mentioned above to run the all peers.


NOTE:

System Requirement:
	1. Linux OS. Tested one ubuntu 16.04
	2. gcc compiler above 4.5. Tested on gcc 5.4 

*****************************************************************************************************************

