CC      ?= cc
KITTY_KEYBOARD_DIR ?= third_party/kitty_keyboard
override CPPFLAGS += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L \
	-I$(KITTY_KEYBOARD_DIR)/include
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
LDLIBS   = -lz -lm -lpthread

SRC = src/main.c src/game.c src/render.c src/term.c src/sound.c
KITTY_OBJ = src/vendor_kitty_keyboard.o src/vendor_kitty_keyboard_posix.o
OBJ = $(SRC:.c=.o) $(KITTY_OBJ)
BIN = kitty-brokeout

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c src/kitty_brokeout.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

src/render.o: src/font8x16.h
src/term.o: $(KITTY_KEYBOARD_DIR)/include/kitty_keyboard.h \
	$(KITTY_KEYBOARD_DIR)/include/kitty_keyboard_posix.h

src/vendor_kitty_keyboard.o: $(KITTY_KEYBOARD_DIR)/src/kitty_keyboard.c \
	$(KITTY_KEYBOARD_DIR)/include/kitty_keyboard.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

src/vendor_kitty_keyboard_posix.o: $(KITTY_KEYBOARD_DIR)/src/kitty_keyboard_posix.c \
	$(KITTY_KEYBOARD_DIR)/include/kitty_keyboard.h \
	$(KITTY_KEYBOARD_DIR)/include/kitty_keyboard_posix.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

test: $(BIN)
	./$(BIN) --input-test
	./$(BIN) --selftest 1337 7200
	./$(BIN) --render-test 42

clean:
	rm -f $(OBJ) $(BIN) render_*.ppm render_*.png

.PHONY: all test clean
