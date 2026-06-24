CART 		?= cart
SRC  		:= $(CART).zig
WASM 		:= $(CART).wasm
SDK 		:= vex.zig
SDK_URL := https://raw.githubusercontent.com/peterhellberg/vex/main/$(SDK)

.PHONY: all build run web clean distclean

all: build

build: $(WASM)

bundle: $(WASM)
	@vex-web -bundle $(WASM)

$(SDK):
	@curl --silent -Lo $@ $(SDK_URL)

$(WASM): $(SRC) $(SDK)
	@zig build-exe \
 		-target wasm32-freestanding \
 		-O ReleaseSmall \
 		-fno-entry \
 		-rdynamic \
 		-femit-bin=$@ \
 		--dep vex \
 		-Mroot=$(SRC) \
 		-Mvex=$(SDK)

run: $(WASM)
	vex -w $(WASM)

web: $(WASM)
	vex-web $(WASM)

clean:
	rm -f $(WASM)

distclean: clean
	rm -f $(SDK)
