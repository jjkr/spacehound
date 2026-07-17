SHELL := /bin/zsh

PROJECT := SpaceRabbit.xcodeproj
PROJECT_SPEC := project.yml
SCHEME := SpaceRabbit
CONFIGURATION ?= Debug
DERIVED_DATA_PATH ?= build/DerivedData
APP_NAME := SpaceRabbit.app
APP_PATH := $(DERIVED_DATA_PATH)/Build/Products/$(CONFIGURATION)/$(APP_NAME)

.PHONY: help generate build release package-release release-script-tests infra-install infra-test run open clean distclean app-path

help:
	@echo "SpaceRabbit development targets"
	@echo ""
	@echo "  make generate           Generate $(PROJECT) from $(PROJECT_SPEC)"
	@echo "  make build              Build $(SCHEME) ($(CONFIGURATION))"
	@echo "  make release            Build $(SCHEME) with CONFIGURATION=Release (unsigned)"
	@echo "  make package-release    Archive, sign, notarize, and package Release artifacts"
	@echo "  make release-script-tests  Test release version validation"
	@echo "  make infra-install      Install pinned CDK dependencies with mise/pnpm"
	@echo "  make infra-test         Type-check and test the CDK stack"
	@echo "  make run                Build and launch the app bundle"
	@echo "  make open               Launch the existing built app bundle"
	@echo "  make clean              Remove repo-local build artifacts"
	@echo "  make distclean          Remove build artifacts and generated Xcode project"
	@echo "  make app-path           Print the built app path"
	@echo ""
	@echo "Overrides:"
	@echo "  CONFIGURATION=Debug|Release"
	@echo "  DERIVED_DATA_PATH=build/DerivedData"

generate:
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

release-script-tests:
	./scripts/tests/release-scripts-test.sh

infra-install:
	mise exec -- pnpm --dir infra install --frozen-lockfile

infra-test:
	mise exec -- pnpm --dir infra run build
	mise exec -- pnpm --dir infra test

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
