SHELL := /bin/zsh

PROJECT := SpaceRabbit.xcodeproj
PROJECT_SPEC := project.yml
SCHEME := SpaceRabbit
CONFIGURATION ?= Debug
DERIVED_DATA_PATH ?= build/DerivedData
APP_NAME := SpaceRabbit.app
APP_PATH := $(DERIVED_DATA_PATH)/Build/Products/$(CONFIGURATION)/$(APP_NAME)
BUNDLED_DAEMON_PATH := $(APP_PATH)/Contents/Helpers/spacerabbitd

.PHONY: help generate build release package-release run open clean distclean app-path bundle-daemon-path daemon-check

help:
	@echo "SpaceRabbit development targets"
	@echo ""
	@echo "  make generate           Generate $(PROJECT) from $(PROJECT_SPEC)"
	@echo "  make build              Build $(SCHEME) ($(CONFIGURATION))"
	@echo "  make release            Build $(SCHEME) with CONFIGURATION=Release (unsigned)"
	@echo "  make package-release    Archive, sign, notarize, and package Release artifacts"
	@echo "  make run                Build and launch the app bundle"
	@echo "  make open               Launch the existing built app bundle"
	@echo "  make clean              Remove repo-local build artifacts"
	@echo "  make distclean          Remove build artifacts and generated Xcode project"
	@echo "  make app-path           Print the built app path"
	@echo "  make bundle-daemon-path Print the bundled daemon path"
	@echo "  make daemon-check       Verify the built app contains spacerabbitd"
	@echo ""
	@echo "Overrides:"
	@echo "  CONFIGURATION=Debug|Release"
	@echo "  DERIVED_DATA_PATH=build/DerivedData"
	@echo "  SPACERABBITD_PATH=/path/to/spacerabbitd"

generate: $(PROJECT)

$(PROJECT): $(PROJECT_SPEC)
	xcodegen generate

build: generate
	xcodebuild \
		-project $(PROJECT) \
		-scheme $(SCHEME) \
		-configuration $(CONFIGURATION) \
		-derivedDataPath $(DERIVED_DATA_PATH) \
		CODE_SIGNING_ALLOWED=NO \
		build

release:
	$(MAKE) build CONFIGURATION=Release

package-release: generate
	./scripts/package-release.sh

run: build
	open "$(APP_PATH)"

open:
	open "$(APP_PATH)"

clean:
	rm -rf build

distclean: clean
	rm -rf "$(PROJECT)"

app-path:
	@echo "$(APP_PATH)"

bundle-daemon-path:
	@echo "$(BUNDLED_DAEMON_PATH)"

daemon-check: build
	@test -x "$(BUNDLED_DAEMON_PATH)"
	@echo "Bundled daemon OK: $(BUNDLED_DAEMON_PATH)"
