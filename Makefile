# Default options
popcnt = yes
memalign = yes
prefetch = yes
variant = release

# Command line arguments
.PHONY : arch
ifeq ($(arch), x86)
    popcnt = no
    CFLAGS += -m32
    ARCH += -m32
else
ifeq ($(arch), x86-64)
    CFLAGS += -m64
    ARCH += -m64
endif
endif
.PHONY : popcnt
ifeq ($(popcnt), yes)
    CPPFLAGS += -DHAS_POPCNT
    CFLAGS += -msse3 -mpopcnt
else
    CPPFLAGS += -DTB_NO_HW_POP_COUNT
endif
.PHONY : memalign
ifeq ($(memalign), yes)
    CPPFLAGS += -DHAS_ALIGNED_MALLOC
endif
.PHONY : prefetch
ifeq ($(prefetch), yes)
    CPPFLAGS += -DHAS_PREFETCH
endif
.PHONY : variant
ifeq ($(variant), release)
    CPPFLAGS += -DNDEBUG
    CFLAGS += -O3 -funroll-loops -fomit-frame-pointer
    LDFLAGS += $(ARCH)
else
ifeq ($(variant), debug)
    CFLAGS += -g
    LDFLAGS += $(ARCH)
else
ifeq ($(variant), profile)
    CPPFLAGS += -DNDEBUG
    CFLAGS += -g -pg -O2 -funroll-loops
    LDFLAGS += $(ARCH) -pg
endif
endif
endif

# Set special flags needed for Windows
ifeq ($(OS), Windows_NT)
CFLAGS += -DWINDOWS
endif

# Configure warnings
CFLAGS += -W -Wall -Werror -Wno-array-bounds -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast

# Extra include directories
CFLAGS += -Iimport/fathom -Isrc

# Common linker options
LDFLAGS += -lm

# Compiler
CC = gcc

# Extra libraries need for tuner
TUNER_LIBS = -lpthread

# Sources
SOURCES = src/bitboard.c \
          src/board.c \
          src/chess.c \
          src/debug.c \
          src/engine.c \
          src/eval.c \
          src/evalparams.c \
          src/fen.c \
          src/hash.c \
          src/key.c \
          src/main.c \
          src/movegen.c \
          src/moveselect.c \
          src/polybook.c \
          src/search.c \
          src/see.c \
          src/test.c \
          src/timectl.c \
          src/uci.c \
          src/utils.c \
          src/validation.c \
          src/xboard.c \
          import/fathom/tbprobe.c
TUNER_SOURCES = src/bitboard.c \
                src/board.c \
                src/chess.c \
                src/debug.c \
                src/engine.c \
                src/eval.c \
                src/evalparams.c \
                src/fen.c \
                src/hash.c \
                src/key.c \
                src/movegen.c \
                src/moveselect.c \
                src/polybook.c \
                src/search.c \
                src/see.c \
                src/test.c \
                src/timectl.c \
                src/tuner.c \
                src/tuningparam.c \
                src/uci.c \
                src/utils.c \
                src/validation.c \
                src/xboard.c \
                import/fathom/tbprobe.c

# Intermediate files
OBJECTS = $(SOURCES:%.c=%.o)
DEPS = $(SOURCES:%.c=%.d)
TUNER_OBJECTS = $(TUNER_SOURCES:%.c=%.o)
TUNER_DEPS = $(TUNER_SOURCES:%.c=%.d)
INTERMEDIATES = $(OBJECTS) $(DEPS)
TUNER_INTERMEDIATES = $(TUNER_OBJECTS) $(TUNER_DEPS)

# Include depencies
-include $(SOURCES:.c=.d)
-include $(TUNER_SOURCES:.c=.d)

# Targets
.DEFAULT_GOAL = marvin

%.o : %.c
	$(COMPILE.c) -MD -o $@ $<
	@cp $*.d $*.d 2> /dev/null; \
            sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
                -e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.d; \

clean :
	rm -f marvin marvin.exe tuner $(INTERMEDIATES) $(TUNER_INTERMEDIATES)
.PHONY : clean

help :
	@echo "make <target> <option>=<value>"
	@echo ""
	@echo "Supported targets:"
	@echo "  marvin: Build the engine."
	@echo "  tuner: Build the tuner program."
	@echo "  help: Display this message."
	@echo "  clean: Remove all intermediate files."
	@echo ""
	@echo "Supported options:"
	@echo "  popcnt=[yes|no]: Use the popcnt HW instruction."
	@echo "  arch=[x86|x86-64]: The architecture to build for."
	@echo "  variant=[release|debug|profile]: The variant to build."
.PHONY : help

marvin : $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o marvin

ifeq ($(OS), Windows_NT)
tuner :
	@echo "Tuner is not supported on Windows"
else
tuner : $(TUNER_OBJECTS)
	$(CC) $(TUNER_OBJECTS) $(LDFLAGS) $(TUNER_LIBS) -o tuner
endif
