CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror
CPPFLAGS += -I../app $(shell pkg-config --cflags sdl2)
LDLIBS += $(shell pkg-config --libs sdl2)

SRC = main.c ../app/passport_store.c ../app/passport_ui.c ../app/openclaw_frame.c ../app/passport_usb.c ../app/passport_pair.c ../app/passport_proxy.c
HOST_SRC = portal_host.c ../app/passport_store.c ../app/passport_proxy.c
PTY_SRC = usb_pty.c ../app/passport_store.c ../app/passport_usb.c ../app/passport_pair.c ../app/passport_proxy.c
SOCK_SRC = config_sock.c ../app/passport_store.c ../app/passport_usb.c ../app/passport_pair.c ../app/passport_proxy.c

.PHONY: all clean
all: passport-sim portal-host usb-pty config-sock

passport-sim: $(SRC) ../app/passport.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(SRC) $(LDLIBS)

portal-host: $(HOST_SRC) ../app/passport.h
	$(CC) $(CFLAGS) -I../app -o $@ $(HOST_SRC)

usb-pty: $(PTY_SRC) ../app/passport.h
	$(CC) $(CFLAGS) -I../app -o $@ $(PTY_SRC)

config-sock: $(SOCK_SRC) ../app/passport.h ../app/passport_control.h
	$(CC) $(CFLAGS) -I../app -o $@ $(SOCK_SRC)

clean:
	rm -f passport-sim portal-host usb-pty config-sock
