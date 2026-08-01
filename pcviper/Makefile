# NEC PC-Viper emulator (fase 1: bus MIPS + núcleo VR5432)
CC      = gcc
CFLAGS  = -std=c11 -O2 -g -Wall -Wextra -Iinclude
LDFLAGS = -lm

BUILD   = build
BIN     = pcviper_emulator

SRCS = src/main.c src/vr5432.c src/bus.c
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
	./$(BIN)

clean:
	rm -rf $(BUILD) $(BIN)
