CC = gcc

build: isfs.o
	$(CC) -Wall isfs.o `pkg-config fuse --cflags --libs` -o isfs
	echo 'To Mount: ./isfs -f [mount point]'

isfs.o: isfs_config.h isfs.c
	gcc -Wall -c -g -D_FILE_OFFSET_BITS=64 isfs.c

clean:
	rm isfs
