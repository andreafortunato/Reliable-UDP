all:
	gcc -o client ClientUDP.c -lpthread -Wall
	gcc -o server ServerUDP.c -lpthread -Wall

clean:
	rm client
	rm server