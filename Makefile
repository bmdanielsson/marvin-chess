# Default build configuration
arch = x86-64-modern
trace = no
variant = release
version = 5.2.0
nnuenet = net-9ef737e9.nnue

# Default options
sse = no
sse2 = no
ssse3 = no
sse41 = no
avx2 = no
popcnt = no

# Set options based on selected architecture
.PHONY : arch
ifeq ($(arch), generic-64)
    APP_ARCH = \"generic-64\"
else
ifeq ($(arch), x86-64)
    sse = yes
    sse2 = yes
    APP_ARCH = \"x86-64\"
else
ifeq ($(arch), x86-64-modern)
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    popcnt = yes
    APP_ARCH = \"x86-64-modern\"
    CPPFLAGS += -DUSE_SSE
else
ifeq ($(arch), x86-64-avx2)
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    avx2 = yes
    popcnt = yes
    APP_ARCH = \"x86-64-avx2\"
    CPPFLAGS += -DUSE_AVX2
endif
endif
endif
endif

# Common flags
ARCH += -m64
CPPFLAGS += -DAPP_ARCH=$(APP_ARCH)
CPPFLAGS += -DNETFILE_NAME=\"$(nnuenet)\"
CPPFLAGS += -DAPP_VERSION=\"$(version)\"
CFLAGS += -m64 -DIS_64BIT
LDFLAGS += -m64

# Set compiler flags based on options
.PHONY : sse
ifeq ($(sse), yes)
    CFLAGS += -msse
endif
.PHONY : sse2
ifeq ($(sse2), yes)
    CFLAGS += -msse2
endif
.PHONY : ssse3
ifeq ($(ssse3), yes)
    CFLAGS += -mssse3
endif
.PHONY : sse41
ifeq ($(sse41), yes)
    CFLAGS += -msse4.1
endif
.PHONY : avx2
ifeq ($(avx2), yes)
    CFLAGS += -mavx2
endif
.PHONY : popcnt
ifeq ($(popcnt), yes)
    CPPFLAGS += -DUSE_POPCNT
    CFLAGS += -msse3 -mpopcnt
else
    CPPFLAGS += -DTB_NO_HW_POP_COUNT
endif
.PHONY : trace
ifeq ($(trace), yes)
    CPPFLAGS += -DTRACE
endif

# Update flags based on build variant
.PHONY : variant
ifeq ($(variant), release)
    CPPFLAGS += -DNDEBUG
    CFLAGS += -O3 -funroll-loops -fomit-frame-pointer $(EXTRACFLAGS)
    LDFLAGS += $(EXTRALDFLAGS)
else
ifeq ($(variant), debug)
    CFLAGS += -g
else
ifeq ($(variant), profile)
    CPPFLAGS += -DNDEBUG
    CFLAGS += -g -pg -O2 -funroll-loops
    LDFLAGS += -pg
endif
endif
endif

# Set special flags needed for different operating systems
ifeq ($(OS), Windows_NT)
    CFLAGS += -DWINDOWS -D_CRT_SECURE_NO_DEPRECATE
    EXEFILE = marvin.exe
else
ifeq ($(variant), release)
    CFLAGS += -flto
    LDFLAGS += -flto
endif
    LDFLAGS += -lpthread -lm
    EXEFILE = marvin
endif

# Configure warnings
CFLAGS += -W -Wall -Werror -Wno-array-bounds -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast

# Extra include directories
CFLAGS += -Iimport/fathom -Iimport/incbin -Isrc

# Enable evaluation tracing for tuner
ifeq ($(MAKECMDGOALS), tuner)
    CFLAGS += -DTRACE
endif

# Compiler
ifeq ($(OS), Windows_NT)
    CC = gcc
else
    CC = clang
endif

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
          src/history.c \
          src/key.c \
          src/movegen.c \
          src/moveselect.c \
          src/nnue.c \
          src/polybook.c \
          src/quantization.c \
          src/search.c \
          src/see.c \
          src/simd.c \
          src/smp.c \
          src/test.c \
          src/thread.c \
          src/timectl.c \
          src/uci.c \
          src/utils.c \
          src/validation.c \
          src/xboard.c \
          import/fathom/tbprobe.c
ENGINE_SOURCES = src/main.c
TUNER_SOURCES = src/trace.c \
                src/tuner.c \
                src/tuningparam.c
.PHONY : trace
ifeq ($(trace), yes)
    SOURCES += src/trace.c src/tuningparam.c
endif

# Intermediate files
OBJECTS = $(SOURCES:%.c=%.o)
DEPS = $(SOURCES:%.c=%.d)
ENGINE_OBJECTS = $(ENGINE_SOURCES:%.c=%.o)
ENGINE_DEPS = $(ENGINE_SOURCES:%.c=%.d)
TUNER_OBJECTS = $(TUNER_SOURCES:%.c=%.o)
TUNER_DEPS = $(TUNER_SOURCES:%.c=%.d)
INTERMEDIATES = $(OBJECTS) $(DEPS)
ENGINE_INTERMEDIATES = $(ENGINE_OBJECTS) $(ENGINE_DEPS)
TUNER_INTERMEDIATES = $(TUNER_OBJECTS) $(TUNER_DEPS)

# Include depencies
-include $(SOURCES:.c=.d)
-include $(ENGINE_SOURCES:.c=.d)
-include $(TUNER_SOURCES:.c=.d)

# Targets
.DEFAULT_GOAL = marvin

%.o : %.c
	$(COMPILE.c) -MD -o $@ $<

clean :
	rm -f $(EXEFILE) tuner $(INTERMEDIATES) $(ENGINE_INTERMEDIATES) $(TUNER_INTERMEDIATES)

.PHONY : clean

help :
	@echo "make <target> <option>=<value>"
	@echo ""
	@echo "Supported targets:"
	@echo "  marvin: Build the engine (default target)."
	@echo "  tuner: Build the tuner program."
	@echo "  net: Fetch the default NNUE net."
	@echo "  help: Display this message."
	@echo "  clean: Remove all intermediate files."
	@echo ""
	@echo "Supported options:"
	@echo "  arch=[generic-64|x86-64|x86-64-modern|x86-64-avx2]: The architecture to build."
	@echo "  trace=[yes|no]: Include support for tracing the evaluation."
	@echo "  variant=[release|debug|profile]: The variant to build."
	@echo "  version=<version>: Override the default version number."
	@echo "  nnuenet=<file>: Override the default NNUE net."
.PHONY : help

marvin : $(OBJECTS) $(ENGINE_OBJECTS)
	$(CC) $(OBJECTS) $(ENGINE_OBJECTS) $(LDFLAGS) -o $(EXEFILE)

tuner : $(OBJECTS) $(TUNER_OBJECTS)
	$(CC) $(OBJECTS) $(TUNER_OBJECTS) $(LDFLAGS) -o tuner

net :
	wget https://github.com/bmdanielsson/marvin-nets/raw/main/v4/$(nnuenet)
