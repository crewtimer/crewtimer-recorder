# CrewTimer Video Recorder

## Build Environment

1. Install nvm to manage node versions.

### Macos

Install brew from [brew.sh](https://brew.sh)

```bash
brew install nvm
brew install nasm
brew install yasm
brew install pkg-config
brew install cmake

nvm install 18
npm i -g yarn
```

## Building CrewTimer Video Recorder

```bash
cd release/app
yarn install
yarn add ../native/recorder # Triggers build for the platform
cd ../..
yarn install
yarn start
```

## Building Native Libraries

If the C++ code is modified and new prebuilt binaries are needed follow the instructions in the [native README](native/recorder/README.md).

After C++ code is modified, do the following to test it:

Edit native/recorder/package.json and bump the version. This will trigger a local build instead of downloading a prebuild release from github (because it won't find one).

```bash
cd release/app
yarn add ../../native/recorder ## Refresh the C++ code in node_modules and build it
cd ../..
yarn start
```

Once you are ready to commit the new C++ code, follow the instructions in the [native README](native/recorder/README.md) to upload to github.

## Releasing new versions

1. Edit [release/app/package.json](release/app/package.json) and adjust version info
2. Execute `yarn macbuild && yarn winbuild`
3. Look in release/ for the dmg and exe files
4. Copy the dmg and exe to a Releases set on github

## Changes to Electron-React-Boilerplate for this application

### extraFiles Section

For production builds an 'extraFiles' section copies the ndi library into the build. For dev builds, a copy:ndi script is used to copy it into the folder used by dev builds under node_modules. This is added to postinstall script execution.

```json
"copy:ndi": "cp native/recorder/lib/libndi.dylib 'node_modules/electron/dist/Electron.app/Contents/Frameworks/Electron Framework.framework/Versions/A/Libraries'",
```

For "mac" section:

```json
"extraFiles": [
        {
          "from": "native/recorder/lib",
          "to": "Frameworks",
          "filter": [
            "libndi.dylib"
          ]
        }
      ]
```

For "win" section:

```json
"target": [
        "nsis"
      ],
      "extraFiles": [
        {
          "from": "native/recorder/lib/Processing.NDI.Lib.x64.dll",
          "to": "Processing.NDI.Lib.x64.dll"
        }
      ]
```

### Firebase support

Add firebase render filter in configs/webpack.config.renderer.dev.dll.ts:

```ts
 renderer: Object.keys(dependencies || {}).filter((it) => it !== 'firebase'),
```

### Markdown md file support

Add md file suffix as a raw-loader to the module.config section of webpack.config.renderer.\*.ts:

```ts
{
        test: /\.md$/,
        use: 'raw-loader',
}
```

### SVG Support

Remove svg section and add to images in webpack.config.renderer.\*.ts:

```ts
{
        test: /\.(png|svg|jpg|jpeg|gif)$/i,
        type: 'asset/resource',
}
```

In package.json add svg to the "moduleNameMapper" suffix list along with png:

```json
"moduleNameMapper": {
      "\\.(jpg|jpeg|svg|png|gif|eot|otf|webp|svg|ttf|woff|woff2|mp4|webm|wav|mp3|m4a|aac|oga)$": "<rootDir>/.erb/mocks/fileMock.js",
      "\\.(css|less|sass|scss)$": "identity-obj-proxy"
},
```

## Code signing for MacOS

In order to install apps downloaded outside the app store without diving into security exception settings, apps must be signed and notarized. This process takes several minutes as the binary must be uploaded to Apple to get notarized. To disable notariztion during development, set `"notarize" : "false"` in the build.mac section of [package.json](package.json)

Requirements for signing:

1. A 'Developer ID Application' signing identity certificate as well as it's associated private key to be in the keychain.
2. Signing credentials with app specific password stored in the keychain.

You can verify you have a valid signing identity with command below. It should list a key such as "Developer ID Application: Entazza LLC (P87Q63DNL4)".

```bash
security find-identity -v -p codesigning
```

Once you have obtained an app specific password from your developer account, store it into the keychain with this command:

```bash
xcrun notarytool store-credentials "crewtimer-app-signing" --apple-id "glenne@engel.org" --team-id P87Q63DNL4 --password app-specific-passord-hash
```

Signing is configured by setting the env variable APPLE_KEYCHAIN_PROFILE to the name of the profile in the keychain that has the credentials. See [macPackager.js](node_modules/app-builder-lib/out/macPackager.js) and [notarize](https://github.com/electron/notarize) for other options.

If notarization fails, add console.log info to [macPackager.js](node_modules/app-builder-lib/out/macPackager.js).

Note: As of 12/2024 the notarize.js script in .erb/scripts is not utilized.
