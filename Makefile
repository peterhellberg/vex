# vex - minimal WASM fantasy console.
#
# The build is defined in build.zig; this Makefile is just a convenience
# wrapper around `zig build`.
#
#   make        build ./vex + cart.wasm + zcart.wasm (into zig-out/bin)
#   make run    run the C example cart
#   make runz   run the Zig example cart
#   make clean  remove build artifacts

.PHONY: all run runz clean

all:
	zig build

run:
	zig build run

runz:
	zig build runz

clean:
	rm -rf zig-out .zig-cache
