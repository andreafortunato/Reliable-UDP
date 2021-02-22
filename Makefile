all:
	mkdir -p ClientDir
	gcc -g -o ClientDir/client ClientUDP.c -lpthread -Wall -Wno-memset-elt-size
	gcc -g -o server ServerUDP.c -lpthread -Wall -Wno-memset-elt-size


client:
	gcc -g -o ClientDir/client ClientUDP.c -lpthread -Wall -Wno-memset-elt-size

server:
	gcc -g -o server ServerUDP.c -lpthread -Wall -Wno-memset-elt-size

clean:
	rm ClientDir/client
	rm server