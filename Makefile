#CC        := gcc
#LD        := gcc

MODULES   := v7 mongoose js-clib
SRC_DIR   := $(addprefix ./,$(MODULES))
BUILD_DIR := $(addprefix build/,$(MODULES))

SRC       := $(foreach sdir,$(SRC_DIR),$(wildcard $(sdir)/*.c))
OBJ       := $(patsubst ./%.c,build/%.o,$(SRC))
INCLUDES  := $(addprefix -I,$(SRC_DIR))
CFLAGS    := -DV7_BUILD_PROFILE=3 -DV7_ENABLE__Memory__stats -DV7_ENABLE_COMPACTING_GC -DV7_ENABLE_FILE -DMG_ENABLE_THREADS -DMG_USE_READ_WRITE
mongooseFLAGS := -DEXCLUDE_COMMON

vpath %.c $(SRC_DIR)
vpath %.h $(SRC_DIR)

define make-goal
$1/%.o: %.c
	$(CC) $(CFLAGS) $($(addsuffix FLAGS,$(notdir $1))) $(INCLUDES) -c $$< -o $$@
endef

.PHONY: all checkdirs clean

all: checkdirs build/jssh

build/jssh: $(OBJ) build/main.o
	$(LD) $^ -lm -lpthread -o $@

build/main.o: main.c
	$(CC) $(CFLAGS) $(INCLUDES) -c main.c -o build/main.o

checkdirs: $(BUILD_DIR)

$(BUILD_DIR):
	@mkdir -p $@

.PHONY: clean
clean:
	@rm -rf $(BUILD_DIR)
	@rm -f build/*

$(foreach bdir,$(BUILD_DIR),$(eval $(call make-goal,$(bdir))))

