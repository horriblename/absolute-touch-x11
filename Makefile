CC=gcc
LIBS=-lxdo

absolute-touch-x11: absolute-touch-x11.c
	$(CC) $(LIBS) -o $@ $@.c
