BIN = display-rotation-menu
SRC = main.c xcbsource.c
OBJ = $(SRC:.c=.o)
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

$(BIN):: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

install: all
	install -m 0755 $(BIN) $(DESTDIR)$(BINDIR)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(BIN) $(OBJ)

.PHONY: all clean install uninstall
