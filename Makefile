# Makefile for gpsstats

TARGET_EXEC = gpsstats

BUILD_DIR = build
SRC_DIRS = src
INC_DIRS = src/inc

SRCS := $(shell find $(SRC_DIRS) -name '*.c')
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

INC_FLAGS := $(addprefix -I,$(INC_DIRS))

CFLAGS += -Wall -Wstrict-prototypes -Wmissing-prototypes -Wshadow -Wconversion
CPPFLAGS += $(INC_FLAGS) -MMD -MP
LDFLAGS = -lgps -lyaml -lmosquitto

all: $(BUILD_DIR)/$(TARGET_EXEC)

$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# c source
$(BUILD_DIR)/%.c.o: %.c
	$(MKDIR_P) $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

.PHONY: clean

clean:
	$(RM) -r $(BUILD_DIR)

-include $(DEPS)

MKDIR_P ?= @mkdir -p

###EOF###
