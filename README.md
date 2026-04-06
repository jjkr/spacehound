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
`~/work/spacerabbit-core` and will be integrated here in a later step.

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
launches as a menu bar item and can be exited from `Quit SpaceRabbit`.
