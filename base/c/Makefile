CART 		?= cart
SRC  		:= $(CART).c
WASM 		:= $(CART).wasm
SDK 		:= vex.h
SDK_URL := https://raw.githubusercontent.com/peterhellberg/vex/main/$(SDK)

.PHONY: all build run web clean distclean

all: build

build: $(WASM)

bundle: $(WASM)
	@vex-web -bundle $(WASM)

$(SDK):
	@curl --silent -Lo $@ $(SDK_URL)

$(WASM): $(SRC) $(SDK)
	@zig cc  \
		--target=wasm32-freestanding \
		-nostdlib \
		-Os \
		-Wl,--no-entry \
		-I. \
		-o $(WASM) $(SRC)

run: $(WASM)
	vex -w $(WASM)

web: $(WASM)
	vex-web $(WASM)

clean:
	rm -f $(WASM)

distclean: clean
	rm -f $(SDK)
