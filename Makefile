all:
	gcc -o ClientDir/client ClientUDP.c -lpthread -Wall
	gcc -o server ServerUDP.c -lpthread -Wall

clean:
	rm ClientDir/client
	rm server