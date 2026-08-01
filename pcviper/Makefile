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

# MIPS IV cross-toolchain for the firmware/demos
MIPS_CC  := $(shell command -v mips64-elf-gcc 2>/dev/null || echo /opt/libdragon/bin/mips64-elf-gcc)
MIPS_OBJ := $(shell command -v mips64-elf-objcopy 2>/dev/null || echo /opt/libdragon/bin/mips64-elf-objcopy)

.PHONY: all tests demo3d demo3d-headless clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

build-tests: $(BIN)
	$(CC) $(CFLAGS) -o $(BUILD)/cpu_test tests/cpu_test.c src/vr5432.c src/bus.c -lm
	$(CC) $(CFLAGS) -o $(BUILD)/voodoo_test tests/voodoo_test.c \
		src/voodoo2_ec.c src/voodoo_fifo.c -lm
	$(CC) $(CFLAGS) -o $(BUILD)/aureal_test tests/aureal_test.c \
		src/aureal_a3d.c src/bus.c -lm
	$(CC) $(CFLAGS) -o $(BUILD)/viper_test tests/viper_test.c \
		src/viper_system.c src/bus.c -lm
	$(CC) $(CFLAGS) -o $(BUILD)/glide_test tests/glide_test.c \
		src/glide_api.c src/voodoo2_ec.c src/voodoo_fifo.c src/bus.c -lm
	$(CC) $(CFLAGS) -o $(BUILD)/pipeline_test tests/pipeline_test.c \
		src/vr5432.c src/bus.c -lm
	$(CC) $(CFLAGS) -o $(BUILD)/voodoo_adv_test tests/voodoo_adv_test.c \
		src/voodoo2_ec.c src/voodoo_fifo.c -lm

tests: build-tests
	./$(BUILD)/cpu_test
	./$(BUILD)/voodoo_test
	./$(BUILD)/aureal_test
	./$(BUILD)/viper_test
	./$(BUILD)/glide_test
	./$(BUILD)/pipeline_test
	./$(BUILD)/voodoo_adv_test
	./$(BIN)

firmware/demo3d.bin: firmware/demo3d.c firmware/init.S firmware/link.ld
	$(MIPS_CC) -EL -mips4 -mhard-float -ffreestanding -nostdlib \
		-mno-abicalls -G0 -O2 -T firmware/link.ld \
		-o firmware/demo3d.elf firmware/init.S firmware/demo3d.c
	$(MIPS_OBJ) -O binary firmware/demo3d.elf firmware/demo3d.bin

demo3d: all firmware/demo3d.bin
	./$(BIN) firmware/demo3d.bin --sdl

demo3d-headless: all firmware/demo3d.bin
	PCVIPER_SDL_FRAMES=4 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
		./$(BIN) firmware/demo3d.bin --sdl

clean:
	rm -rf $(BUILD) $(BIN)
