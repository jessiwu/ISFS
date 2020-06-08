CC = gcc

build: isfs.o
	$(CC) -Wall isfs.o -o isfs `pkg-config fuse --cflags --libs`
	echo 'To Mount: ./isfs -f [mount point]'

isfs.o: isfs.c
	gcc -Wall -c -g -D_FILE_OFFSET_BITS=64 isfs.c

clean:
	rm isfs
