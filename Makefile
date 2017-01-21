CC = gcc
CFLAGS = -Og -g -std=c99 -DFP_TYPE=float
OBJS = vnucp.o ciglet.o
TARGETS = vnucpe vnucpd

default: $(TARGETS)
test: test-cbuffer

vnucpe: $(OBJS) vnucpe.c
	$(CC) $(CFLAGS) -o vnucpe vnucpe.c $(OBJS) -lm

vnucpd: $(OBJS) vnucpd.c
	$(CC) $(CFLAGS) -o vnucpd vnucpd.c $(OBJS) -lm

test-cbuffer: $(OBJS) test/test-cbuffer.c
	$(CC) $(CFLAGS) -o test-cbuffer test/test-cbuffer.c $(OBJS) -lm

ciglet.o: external/ciglet/ciglet.c external/ciglet/ciglet.h
	$(CC) $(CFLAGS) -o ciglet.o -c external/ciglet/ciglet.c

vnucp.o: vnucp.c vnucp.h

%.o : %.c
	$(CC) $(CFLAGS) -o $*.o -c $*.c

clean:
	@echo 'Removing all temporary binaries...'
	@rm -f *.a *.o $(TARGETS)
	@echo Done.
