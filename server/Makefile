
CC = gcc

ROOT_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

TARGET = $(ROOT_DIR)build/server
BUILD_DIR = $(ROOT_DIR)build

CPPFLAGS += -I$(ROOT_DIR)include
CFLAGS += -std=gnu11 -Wall -Wextra -Wpedantic -pthread
LDFLAGS += -pthread
LDLIBS += -lcrypto

SOURCES := $(shell find $(ROOT_DIR)src -type f -name '*.c')
OBJECTS := $(patsubst $(ROOT_DIR)src/%.c,$(BUILD_DIR)/obj/%.o,$(SOURCES))
DEPFILES := $(OBJECTS:.o=.d)

.PHONY: all clean rebuild run

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	@echo "Server built successfully."

$(BUILD_DIR)/obj/%.o: $(ROOT_DIR)src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPFILES)

run: $(TARGET)
	cd $(ROOT_DIR) && ./build/server

clean:
	rm -rf $(BUILD_DIR)

rebuild: clean all

