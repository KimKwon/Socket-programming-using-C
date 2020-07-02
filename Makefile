server	:	server.o
	gcc	-g	-W	-Wall	-o	server	server.o
server.o	:	server.c
	gcc	-g	-W	-Wall	-c	-o	server.o	server.c