# Default build configuration
arch = x86-64-modern
variant = release
version = 6.2.0
nnuenet = net-99b1529.nnue

# Default options
sse = no
sse2 = no
ssse3 = no
sse41 = no
avx2 = no

# Set options based on selected architecture
.PHONY : arch
ifeq ($(arch), generic-64)
    APP_ARCH = \"generic-64\"
    CPPFLAGS += -DIS_64BIT -DTB_NO_HW_POP_COUNT
    CFLAGS += -m64
    LDFLAGS += -m64
else
ifeq ($(arch), x86-64)
    sse = yes
    sse2 = yes
    APP_ARCH = \"x86-64\"
    CPPFLAGS += -DIS_64BIT -DTB_NO_HW_POP_COUNT
    CFLAGS += -m64
    LDFLAGS += -m64
else
ifeq ($(arch), x86-64-modern)
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    APP_ARCH = \"x86-64-modern\"
    CPPFLAGS += -DUSE_SSE -DIS_64BIT -DUSE_POPCNT
    CFLAGS += -m64 -msse3 -mpopcnt
    LDFLAGS += -m64
else
ifeq ($(arch), x86-64-avx2)
    sse = yes
    sse2 = yes
    ssse3 = yes
    sse41 = yes
    avx2 = yes
    popcnt = yes
    APP_ARCH = \"x86-64-avx2\"
    CPPFLAGS += -DUSE_AVX2 -DIS_64BIT -DUSE_POPCNT
    CFLAGS += -m64 -msse3 -mpopcnt
    LDFLAGS += -m64
else
ifeq ($(arch), aarch64)
    APP_ARCH = \"aarch64\"
    CPPFLAGS += -DIS_64BIT -DUSE_POPCNT -DUSE_NEON
endif
endif
endif
endif
endif

# Common flags
CPPFLAGS += -DAPP_ARCH=$(APP_ARCH)
CPPFLAGS += -DNETFILE_NAME=\"$(nnuenet)\"
CPPFLAGS += -DAPP_VERSION=\"$(version)\"

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
    RM = del /f /q
    SEP = \\
else
ifeq ($(variant), release)
    CFLAGS += -flto
    LDFLAGS += -flto
endif
    RM = rm -f
    SEP = /
    LDFLAGS += -lpthread -lm
    EXEFILE = marvin
endif
PSEP = $(strip $(SEP))

# Configure warnings
CFLAGS += -W -Wall -Werror -Wno-array-bounds -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast

# Extra include directories
CFLAGS += -Iimport/fathom -Iimport/incbin -Isrc

# Compiler
ifeq ($(OS), Windows_NT)
    CC = gcc
else
ifeq ($(arch), aarch64)
    CC = gcc
else
    CC = clang
endif
endif

# Sources
SOURCES = src/bitboard.c \
          src/data.c \
          src/debug.c \
          src/egtb.c \
          src/engine.c \
          src/eval.c \
          src/evalparams.c \
          src/fen.c \
          src/hash.c \
          src/history.c \
          src/key.c \
          src/main.c \
          src/movegen.c \
          src/moveselect.c \
          src/nnue.c \
          src/polybook.c \
          src/position.c \
          src/search.c \
          src/see.c \
          src/sfen.c \
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

# Intermediate files
OBJECTS = $(SOURCES:%.c=%.o)
DEPS = $(SOURCES:%.c=%.d)
INTERMEDIATES = $(OBJECTS) $(DEPS)

# Include depencies
-include $(SOURCES:.c=.d)

# Targets
.DEFAULT_GOAL = marvin

%.o : %.c
	$(COMPILE.c) -MD -o $@ $<

clean :
	$(RM) $(EXEFILE) $(subst /,$(SEP),$(INTERMEDIATES))

.PHONY : clean

help :
	@echo "make <target> <option>=<value>"
	@echo ""
	@echo "Supported targets:"
	@echo "  marvin: Build the engine (default target)."
	@echo "  net: Fetch the default NNUE net."
	@echo "  help: Display this message."
	@echo "  clean: Remove all intermediate files."
	@echo ""
	@echo "Supported options:"
	@echo "  arch=[generic-64|x86-64|x86-64-modern|x86-64-avx2]: The architecture to build."
	@echo "  variant=[release|debug|profile]: The variant to build."
	@echo "  version=<version>: Override the default version number."
	@echo "  nnuenet=<file>: Override the default NNUE net."
.PHONY : help

marvin : $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $(EXEFILE)

net :
	wget https://github.com/bmdanielsson/marvin-nets/raw/main/v8/$(nnuenet)
