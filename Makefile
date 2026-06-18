# vex - minimal WASM fantasy console.
#
#   make          build the console (./vex) and the example cart (cart.wasm)
#   make run      build everything and run the example cart
#   make clean    remove build artifacts
#   make distclean  also remove the vendored wasm3 checkout

CC  := zig cc
ZIG := zig

# --- wasm3: the embedded WASM runtime (fetched on first build) --------------
WASM3_VER := v0.5.0
WASM3_DIR := vendor/wasm3
WASM3_SRC := $(WASM3_DIR)/source
# Core interpreter only; the optional m3_api_*.c modules (WASI/libc/tracer)
# are not needed since the console provides its own host functions.
WASM3_SOURCES = $(filter-out $(WASM3_SRC)/m3_api_%.c, $(wildcard $(WASM3_SRC)/*.c))

# --- raylib (via pkg-config) -----------------------------------------------
RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib)

ifeq ($(shell uname -s),Darwin)
  RAYLIB_LIBS   := $(shell pkg-config --libs raylib)
  PLATFORM_LIBS := -framework CoreVideo -framework IOKit -framework Cocoa -framework OpenGL
else
  # Linux: "--static --libs" reports raylib's transitive dependencies (GL,
  # X11/Wayland, etc.) that live in the .pc Libs.private field; the libraries
  # themselves are still linked dynamically. Fall back to plain --libs if that
  # query fails. -lm -lpthread -ldl -lrt cover raylib's own system deps.
  RAYLIB_LIBS   := $(shell pkg-config --static --libs raylib 2>/dev/null || pkg-config --libs raylib)
  PLATFORM_LIBS := -lm -lpthread -ldl -lrt
endif

CFLAGS  := -O2 -std=c11 -I$(WASM3_SRC) $(RAYLIB_CFLAGS)
LDFLAGS := $(RAYLIB_LIBS) $(PLATFORM_LIBS)

# Build a cart C source into a wasm32 module.
CART_FLAGS := --target=wasm32-freestanding -nostdlib -O2 -Wl,--no-entry -I.

.PHONY: all run runz clean distclean
all: vex cart.wasm zcart.wasm

vex: main.c | $(WASM3_DIR)
	$(CC) $(CFLAGS) -o $@ main.c $(WASM3_SOURCES) $(LDFLAGS)

cart.wasm: cart/main.c vex.h
	$(CC) $(CART_FLAGS) -o $@ cart/main.c

# Zig cart: build a freestanding wasm32 module, exporting boot()/update().
zcart.wasm: zcart/main.zig
	$(ZIG) build-exe $< -target wasm32-freestanding -O ReleaseSmall \
		-fno-entry -rdynamic -femit-bin=$@

$(WASM3_DIR):
	git clone --depth 1 --branch $(WASM3_VER) https://github.com/wasm3/wasm3 $(WASM3_DIR)

run: all
	./vex cart.wasm

runz: all
	./vex zcart.wasm

clean:
	rm -f vex cart.wasm zcart.wasm

distclean: clean
	rm -rf vendor .zig-cache zig-cache
