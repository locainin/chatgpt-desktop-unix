BUILD_DIR ?= build
BUILD_TYPE ?= Release
INSTALL_PREFIX ?= $(HOME)/.local
JOBS ?= $(shell nproc)
CMAKE ?= cmake
CTEST ?= ctest
CPPCHECK ?= cppcheck
VALGRIND ?= valgrind

.PHONY: all build configure install uninstall clean test check sanitize help

all: build

configure:
	$(CMAKE) -S . -B "$(BUILD_DIR)" \
		-DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" \
		-DCMAKE_INSTALL_PREFIX="$(INSTALL_PREFIX)"

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" --parallel "$(JOBS)"

install: build
	$(CMAKE) --install "$(BUILD_DIR)"

uninstall: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target uninstall
	$(CMAKE) -E rm -rf "$(BUILD_DIR)" "$(BUILD_DIR)-sanitize" compile_commands.json

test: build
	$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure

check: test
	$(CPPCHECK) src qa --enable=all --inconclusive --std=c++20 \
		--check-level=exhaustive --quiet --error-exitcode=1 \
		--suppress=missingIncludeSystem --suppress=checkersReport \
		-DQT_VERSION=0x060b01 '-DQT_VERSION_CHECK(major,minor,patch)=((major<<16)|(minor<<8)|(patch))' \
		-I src
	$(VALGRIND) --leak-check=full --show-leak-kinds=definite,indirect,possible \
		--errors-for-leak-kinds=definite,indirect,possible --error-exitcode=1 \
		"$(BUILD_DIR)/chatgpt-desktop-unix-system-browser-launcher-tests"

sanitize:
	$(CMAKE) -S . -B "$(BUILD_DIR)-sanitize" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_INSTALL_PREFIX="$(INSTALL_PREFIX)" \
		-DCHATGPT_DESKTOP_ENABLE_SANITIZERS=ON -DCHATGPT_DESKTOP_ENABLE_IPO=OFF
	$(CMAKE) --build "$(BUILD_DIR)-sanitize" --parallel "$(JOBS)"
	$(CTEST) --test-dir "$(BUILD_DIR)-sanitize" --output-on-failure

clean:
	$(CMAKE) -E rm -rf "$(BUILD_DIR)" "$(BUILD_DIR)-sanitize" compile_commands.json

help:
	@printf '%s\n' \
		'make             Build a Release binary in ./build' \
		'make install     Build and install into $(INSTALL_PREFIX)' \
		'make uninstall   Remove installed files, app state, and local build output' \
		'make clean       Remove only local build output' \
		'make test        Build and run automated tests' \
		'make check       Run tests, cppcheck, and Valgrind' \
		'make sanitize    Build and test with sanitizers'
