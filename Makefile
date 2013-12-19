all: client server

client: client.o ../nplib/np_lib.o ../nplib/error_functions.o common_lib.o common_lib.h
	gcc -o client client.o  ../nplib/np_lib.o ../nplib/error_functions.o common_lib.o

client.o: client.c common_lib.h
	gcc -c client.c

server: server.o ../nplib/np_lib.o ../nplib/error_functions.o common_lib.o common_lib.h
	gcc -o server server.o  ../nplib/np_lib.o ../nplib/error_functions.o common_lib.o

server.o: server.c common_lib.h
	gcc -c server.c

common.o: common_lib.c common_lib.h
	gcc -c common_lib.c

clean:
	rm *.log *.o
