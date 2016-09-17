#simplest Make file
#todo make libcode.a and libcode.so as libs

CC = gcc
src = $(wildcard *.c)
obj = $(src:.c=.o)

LDFLAGS = -lpthread

test: $(obj)
	$(CC) -o $@ $^ $(LDFLAGS)
	
.PHONY: clean
clean:
	rm -f $(obj) test
