# CrewTimer Video Recorder

## Build Environment

1. Install nvm to manage node versions.

### Macos

```bash
brew install nvm
brew install nasm
brew install yasm
brew install pkg-config
brew install cmake
```

## Building FFmpeg

First checkout the FFmpeg sources:

```bash
git clone https://github.com/FFmpeg/FFmpeg.git
cd FFmpeg
git checkout n6.1.1
```

**Recipe For Mac Apple Silicon** [youtube video](https://www.youtube.com/watch?v=-tRh1ThNxoc)

## Building CrewTimer Video Recorder

```bash
cd release/app
yarn install
yarn add ../native/recorder # Triggers build for the platform
cd ../..
yarn install
yarn start
```

## Releasing macos version

To create a notarized macos build, create a .env file with the following contents.  **Do not commit this file to the repo**

```txt
APPLE_ID=glenne@engel.org
APPLE_APP_SPECIFIC_PASSWORD=xxxx-xxxx-xxxx-xxxx
TEAM_ID=P<snip>4
```

1. Edit release/app/package.json and update the version
2. `yarn macbuild`.  dmg file is placed in release/build
