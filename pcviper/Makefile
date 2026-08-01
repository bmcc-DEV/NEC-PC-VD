# NEC PC-Viper emulator (VR5432 + Voodoo2 EC + Aureal A3D)
CC      = gcc
CFLAGS  = -std=c11 -O2 -g -Wall -Wextra -Iinclude
LDFLAGS = -lm

# SDL2 (optional audio output)
SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL2_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)
ifneq ($(SDL2_CFLAGS),)
CFLAGS += $(SDL2_CFLAGS) -DHAVE_SDL2
LDFLAGS += $(SDL2_LIBS)
endif

BUILD   = build
BIN     = pcviper_emulator

SRCS = src/main.c src/vr5432.c src/bus.c src/voodoo2_ec.c src/voodoo_fifo.c \
       src/aureal_a3d.c src/viper_system.c src/glide_api.c
OBJS = $(SRCS:src/%.c=$(BUILD)/%.o)

.PHONY: all tests clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

tests: $(BIN)
	$(CC) $(CFLAGS) -o $(BUILD)/cpu_test tests/cpu_test.c src/vr5432.c src/bus.c -lm
	./$(BUILD)/cpu_test
	$(CC) $(CFLAGS) -o $(BUILD)/voodoo_test tests/voodoo_test.c \
		src/voodoo2_ec.c src/voodoo_fifo.c -lm
	./$(BUILD)/voodoo_test
	$(CC) $(CFLAGS) -o $(BUILD)/aureal_test tests/aureal_test.c \
		src/aureal_a3d.c src/bus.c -lm
	./$(BUILD)/aureal_test
	$(CC) $(CFLAGS) -o $(BUILD)/viper_test tests/viper_test.c \
		src/viper_system.c src/bus.c -lm
	./$(BUILD)/viper_test
	$(CC) $(CFLAGS) -o $(BUILD)/glide_test tests/glide_test.c \
		src/glide_api.c src/voodoo2_ec.c src/voodoo_fifo.c src/bus.c -lm
	./$(BUILD)/glide_test
	$(CC) $(CFLAGS) -o $(BUILD)/pipeline_test tests/pipeline_test.c \
		src/vr5432.c src/bus.c -lm
	./$(BUILD)/pipeline_test
	./$(BIN)

clean:
	rm -rf $(BUILD) $(BIN)
