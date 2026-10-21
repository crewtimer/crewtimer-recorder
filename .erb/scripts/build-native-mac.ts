// Builds the native recorder for this Mac and installs it into release/app, so a
// Mac package never ships a stale module or the Windows one left by build:win.
import { execSync } from 'child_process';
import fs from 'fs';
import path from 'path';
import webpackPaths from '../configs/webpack.paths';

const nativeDir = path.join(webpackPaths.rootPath, 'native', 'recorder');
// Run through node, not node_modules/.bin, whose shims differ between the Mac
// and Windows installs of the shared native/recorder folder
const nodeGyp = path.join(
  nativeDir,
  'node_modules',
  'node-gyp',
  'bin',
  'node-gyp.js',
);
const moduleFile = path.join(
  'build',
  'Release',
  'crewtimer_video_recorder.node',
);
const built = path.join(nativeDir, moduleFile);
const installed = path.join(
  webpackPaths.appNodeModulesPath,
  'crewtimer_video_recorder',
  moduleFile,
);

const MACHO_64 = 0xfeedfacf;
const CPU_TYPE_ARM64 = 0x0100000c;

/** True if the file is a 64-bit Mach-O binary for Apple silicon. */
const isMacArm64 = (file: string) => {
  try {
    const fd = fs.openSync(file, 'r');
    const header = Buffer.alloc(8);
    fs.readSync(fd, header, 0, 8, 0);
    fs.closeSync(fd);
    return (
      header.readUInt32LE(0) === MACHO_64 &&
      header.readUInt32LE(4) === CPU_TYPE_ARM64
    );
  } catch {
    return false;
  }
};

// Always a clean rebuild: the build folder is shared with Windows builds of the
// same checkout, and the NDI SDK path's spaces break make's incremental
// dependency files on macOS. A full build only takes a few seconds longer.
console.log('Rebuilding native recorder...');
execSync(`"${process.execPath}" "${nodeGyp}" rebuild`, {
  cwd: nativeDir,
  stdio: 'inherit',
});

if (!isMacArm64(built)) {
  throw new Error(`${built} is not a macOS arm64 module`);
}
fs.mkdirSync(path.dirname(installed), { recursive: true });
fs.copyFileSync(built, installed);
console.log(`Installed native recorder into ${installed}`);
