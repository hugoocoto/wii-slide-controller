CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -ggdb -Ilib/virt.h -Ilib/wiimote.h
LDLIBS  = -pthread -lbluetooth

all: a.out

a.out: main.c lib/virt.h/virt.h lib/wiimote.h/wiimote.h
	$(CC) $(CFLAGS) main.c -o $@ $(LDLIBS)

clean:
	rm -f a.out

.PHONY: all clean
