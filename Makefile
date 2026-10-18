PSPDEV ?= $(CURDIR)/_deps/pspdev
ZIG ?= $(CURDIR)/_deps/zig/zig
WASI_SDK ?= $(CURDIR)/_deps/wasi-sdk
TAILWIND ?= $(CURDIR)/_deps/tailwindcss
JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
OUT ?= out
GAME ?=

export PSPDEV
export PATH := $(PSPDEV)/bin:$(PATH)

BUILDER := build/builder-native/omori-builder
ENGINE := build/psp/EBOOT.PBP

.PHONY: all help toolchain zig wasi-sdk tailwind engine desktop builder test run play release web web-serve clean

all: builder

help:
	@echo "make                build the PSP engine and the builder for this machine"
	@echo "make toolchain      download the pspdev toolchain into _deps/pspdev"
	@echo "make zig            download zig into _deps/zig (used by make release)"
	@echo "make wasi-sdk       download wasi-sdk into _deps/wasi-sdk (used by make web)"
	@echo "make tailwind       download the tailwindcss cli into _deps (used by make web)"
	@echo "make engine         build the PSP engine ($(ENGINE))"
	@echo "make desktop        build the desktop test build (needs SDL2, libvorbis, zlib)"
	@echo "make builder        build the builder for this machine ($(BUILDER))"
	@echo "make run            run the builder; GAME=path/to/game and OUT=folder are optional"
	@echo "make play           run the built EBOOT in the desktop build (OUT=folder used by make run)"
	@echo "make test           run the builder unit tests"
	@echo "make release        cross-compile the builder for Linux, Windows and macOS into build/builder"
	@echo "make web            build the in-browser builder page into build/web"
	@echo "make web-serve      serve build/web on http://localhost:8080"
	@echo "make clean          remove build/"

toolchain: $(PSPDEV)/bin/psp-gcc

$(PSPDEV)/bin/psp-gcc:
	./scripts/setup-toolchain.sh

zig: $(ZIG)

$(ZIG):
	./scripts/setup-zig.sh

wasi-sdk: $(WASI_SDK)/bin/clang

$(WASI_SDK)/bin/clang:
	./scripts/setup-wasi-sdk.sh

tailwind: $(TAILWIND)

$(TAILWIND):
	./scripts/setup-tailwind.sh

engine: toolchain
	cmake -S engine -B build/psp -DCMAKE_TOOLCHAIN_FILE=$(PSPDEV)/psp/share/pspdev.cmake -DCMAKE_BUILD_TYPE=Release
	cmake --build build/psp -j$(JOBS)

desktop:
	cmake -S engine -B build/desktop -DCMAKE_BUILD_TYPE=RelWithDebInfo
	cmake --build build/desktop -j$(JOBS)

builder: engine
	cmake -S tools/builder -B build/builder-native -DCMAKE_BUILD_TYPE=Release
	cmake --build build/builder-native -j$(JOBS)

test: builder
	ctest --test-dir build/builder-native --output-on-failure

run: builder
	$(BUILDER) $(if $(GAME),"$(GAME)" -o "$(OUT)")

play: desktop
	build/desktop/omori --data "$(OUT)/PSP/GAME/OMORI/EBOOT.PBP" --saves "$(OUT)/saves"

release: engine zig
	ZIG=$(ZIG) ./scripts/build-builder.sh

web: engine wasi-sdk tailwind
	WASI_SDK=$(WASI_SDK) TAILWIND=$(TAILWIND) ./scripts/build-web.sh

web-serve: web
	cd build/web && python3 -m http.server 8080 --bind 127.0.0.1

clean:
	rm -rf build
