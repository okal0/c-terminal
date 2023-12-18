C = gcc
CFLAGS = -Wall


all: main.c
	$(C) $(CFLAGS) main.c


clean:
	rm -r a.out *.log