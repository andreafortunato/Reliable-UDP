all:
	gcc -g -o ClientDir/client ClientUDP.c -lpthread -Wall -Wno-memset-elt-size
	gcc -g -o server ServerUDP.c -lpthread -Wall -Wno-memset-elt-size

debug:
	gcc -g -o ClientDir/client ClientUDP.c -lpthread -Wall -Wno-memset-elt-size
	gcc -g -o server ServerUDP.c -lpthread -Wall -Wno-memset-elt-size


client:
	gcc -o ClientDir/client ClientUDP.c -lpthread -Wall

server:
	gcc -o server ServerUDP.c -lpthread -Wall

clean:
	rm ClientDir/client
	rm server