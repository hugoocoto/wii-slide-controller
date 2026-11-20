CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -ggdb -Ilib/virt.h -Ilib/wiimote.h
LDLIBS  = -pthread -lbluetooth
PREFIX  = /usr/local

BIN = wii-slide-controller

all: $(BIN)

$(BIN): main.c lib/virt.h/virt.h lib/wiimote.h/wiimote.h
	$(CC) $(CFLAGS) main.c -o $@ $(LDLIBS)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)

clean:
	rm -f $(BIN)

.PHONY: all install uninstall clean
