BIN = display-rotation-menu-gtk
LIBS = gtk+-2.0 xcb xcb-randr
CFLAGS = -Wall -Werror -Wextra -Wno-unused-parameter -std=gnu99 -g \
		 $(shell pkg-config --cflags $(LIBS))
LDFLAGS = $(shell pkg-config --libs $(LIBS))
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

all: $(BIN)

debug:
	@$(MAKE) --no-print-directory
	gdb ./$(BIN)

$(BIN): main.o xcbsource.o

$(BIN):
	$(CC) -o $@ $^ $(LDFLAGS)

install: all
	install -m 0755 $(BIN) $(DESTDIR)$(BINDIR)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(BIN) $(wildcard *.o)

.PHONY: all clean install uninstall
