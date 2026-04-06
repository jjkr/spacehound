<p align="center">
  <img height="150" src="https://github.com/animaslabs/spacerabbit/blob/main/logo.png">
</p>

<h1 align="center">SpaceRabbit</h1>

<p align="center">Fast workspace navigation for macOS.</p>

## Install
Get the latest installer [HERE](https://github.com/animaslabs/spacerabbit/releases/latest).

## Features

- Fast space switching with hotkeys
- Menu bar icon with current workspace number
- More

## Development

This repo contains the macOS menu bar app shell for SpaceRabbit. The workspace daemon lives in
the sibling `../spacerabbit-core` checkout and is packaged into the app at build time.

### Prerequisites

- Xcode 26.4 or newer
- [XcodeGen](https://github.com/yonaskolb/XcodeGen) 2.45.3 or newer

### Generate The Project

```sh
xcodegen generate
```

### Build The App

```sh
xcodebuild -project SpaceRabbit.xcodeproj -scheme SpaceRabbit -configuration Debug -derivedDataPath build/DerivedData CODE_SIGNING_ALLOWED=NO build
```

### Run The App

Open the generated `SpaceRabbit.xcodeproj` in Xcode and run the `SpaceRabbit` scheme. The app
launches as a menu bar item, supervises `spacerabbitd`, and can be exited from `Quit SpaceRabbit`.

### Daemon Development

The app bundles `spacerabbitd` into `SpaceRabbit.app/Contents/Resources/bin/spacerabbitd` at build
time.

By default the Xcode build looks for a local daemon binary at:

- `../spacerabbit-core/build/ninja-debug/spacerabbitd`
- `../spacerabbit-core/build/ninja-debug/package-shared/bin/spacerabbitd`
- `../spacerabbit-core/build/ninja-debug/package-static/bin/spacerabbitd`

You can override the source daemon binary by setting `SPACERABBITD_PATH` in the Xcode scheme
environment or in CI before invoking `xcodebuild`.

For GitHub Actions later, the clean model is: build or download `spacerabbitd` in CI, export its
path as `SPACERABBITD_PATH`, then let the app build package that artifact into the app bundle. Do
not fetch binaries at app runtime.

On first launch the app creates `~/Library/Application Support/SpaceRabbit/settings.json` if it
does not already exist, then launches `spacerabbitd --settings <that path>`.
