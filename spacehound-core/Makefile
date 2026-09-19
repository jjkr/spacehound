# SPDX-FileCopyrightText: 2026 Joe Kramer
# SPDX-License-Identifier: Apache-2.0

.DEFAULT_GOAL := debug

CONFIG ?= debug
VALID_CONFIGS := debug release coverage

ifeq ($(filter $(CONFIG),$(VALID_CONFIGS)),)
$(error Unsupported CONFIG '$(CONFIG)'. Valid values: $(VALID_CONFIGS))
endif

PRESET_debug := ninja-debug
PRESET_release := ninja-release
PRESET_coverage := ninja-coverage

PRESET := $(PRESET_$(CONFIG))
BUILD_DIR := build/$(PRESET)

.PHONY: configure build test coverage-report clean debug release coverage test-debug test-release test-coverage

configure:
	cmake --preset $(PRESET)

build: configure
	cmake --build --preset $(PRESET)

test: build
	ctest --preset $(PRESET)

coverage-report:
	@if [ "$(CONFIG)" != "coverage" ]; then \
		echo "coverage-report requires CONFIG=coverage"; \
		exit 2; \
	fi
	cmake --build $(BUILD_DIR) --target coverage

clean:
	@if [ -d "$(BUILD_DIR)" ]; then \
		cmake --build $(BUILD_DIR) --target clean; \
	else \
		echo "No build tree at $(BUILD_DIR); skipping clean."; \
	fi

debug:
	$(MAKE) build CONFIG=debug

release:
	$(MAKE) build CONFIG=release

coverage:
	$(MAKE) test CONFIG=coverage
	$(MAKE) coverage-report CONFIG=coverage

test-debug:
	$(MAKE) test CONFIG=debug

test-release:
	$(MAKE) test CONFIG=release

test-coverage:
	$(MAKE) test CONFIG=coverage
