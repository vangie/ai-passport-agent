CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror
CPPFLAGS += -I../app $(shell pkg-config --cflags sdl2)
LDLIBS += $(shell pkg-config --libs sdl2)

SRC = main.c ../app/badge_store.c ../app/badge_ui.c ../app/openclaw_frame.c ../app/badge_usb.c ../app/badge_pair.c ../app/badge_proxy.c
HOST_SRC = portal_host.c ../app/badge_store.c ../app/badge_proxy.c
PTY_SRC = usb_pty.c ../app/badge_store.c ../app/badge_usb.c ../app/badge_pair.c ../app/badge_proxy.c
SOCK_SRC = config_sock.c ../app/badge_store.c ../app/badge_usb.c ../app/badge_pair.c ../app/badge_proxy.c

.PHONY: all clean
all: badge-sim portal-host usb-pty config-sock

badge-sim: $(SRC) ../app/badge.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(SRC) $(LDLIBS)

portal-host: $(HOST_SRC) ../app/badge.h
	$(CC) $(CFLAGS) -I../app -o $@ $(HOST_SRC)

usb-pty: $(PTY_SRC) ../app/badge.h
	$(CC) $(CFLAGS) -I../app -o $@ $(PTY_SRC)

config-sock: $(SOCK_SRC) ../app/badge.h
	$(CC) $(CFLAGS) -I../app -o $@ $(SOCK_SRC)

clean:
	rm -f badge-sim portal-host usb-pty config-sock
