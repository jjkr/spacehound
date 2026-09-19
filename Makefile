SHELL := /bin/zsh

PROJECT := SpaceHound.xcodeproj
PROJECT_SPEC := project.yml
SCHEME := SpaceHound
CONFIGURATION ?= Debug
DERIVED_DATA_PATH ?= build/DerivedData
SENTRY_DSN ?=
APP_NAME := SpaceHound.app
APP_PATH := $(DERIVED_DATA_PATH)/Build/Products/$(CONFIGURATION)/$(APP_NAME)

.PHONY: help generate build release package-release release-script-tests updates-deploy updates-login run open clean distclean app-path

help:
	@echo "SpaceHound development targets"
	@echo ""
	@echo "  make generate           Generate $(PROJECT) from $(PROJECT_SPEC)"
	@echo "  make build              Build $(SCHEME) ($(CONFIGURATION))"
	@echo "  make release            Build $(SCHEME) with CONFIGURATION=Release (unsigned)"
	@echo "  make package-release    Archive, sign, notarize, and package Release artifacts"
	@echo "  make release-script-tests  Test the release scripts"
	@echo "  make updates-deploy     Deploy updates/public to the Cloudflare Worker (bootstrap or recovery)"
	@echo "  make updates-login      Authenticate wrangler with your Cloudflare account"
	@echo "  make run                Build and launch the app bundle"
	@echo "  make open               Launch the existing built app bundle"
	@echo "  make clean              Remove repo-local build artifacts"
	@echo "  make distclean          Remove build artifacts and generated Xcode project"
	@echo "  make app-path           Print the built app path"
	@echo ""
	@echo "Overrides:"
	@echo "  CONFIGURATION=Debug|Release"
	@echo "  DERIVED_DATA_PATH=build/DerivedData"
	@echo "  SENTRY_DSN=public DSN for opt-in local crash reporting"

generate:
	xcodegen generate

build: generate
	xcodebuild \
		-project $(PROJECT) \
		-scheme $(SCHEME) \
		-configuration $(CONFIGURATION) \
		-derivedDataPath $(DERIVED_DATA_PATH) \
		SENTRY_DSN="$(SENTRY_DSN)" \
		CODE_SIGNING_ALLOWED=NO \
		build

release:
	$(MAKE) build CONFIGURATION=Release

package-release: generate
	./scripts/package-release.sh

release-script-tests:
	./scripts/tests/release-scripts-test.sh

# Deploys whatever is in updates/public. Normal releases deploy from CI. Use
# this once to create the Worker and its custom domain before the first
# release, or to recover after putting the intended appcast.xml in place.
# Authenticate first with `make updates-login` or CLOUDFLARE_API_TOKEN.
updates-deploy:
	cd updates && mise exec -- npx --yes wrangler@4 deploy

updates-login:
	cd updates && mise exec -- npx --yes wrangler@4 login

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
