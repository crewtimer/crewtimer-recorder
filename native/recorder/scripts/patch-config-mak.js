// Git Bash (MSYS) fixup for FFmpeg's dependency-tracking awk, run by build-ffmpeg.sh
// after configure. FFmpeg's ffbuild/config.mak runs an inline awk program whose regex
// contains a backslash; MSYS bash strips it, so every compile fails with
// "awk: unterminated regexp". Moving the program into a file avoids the shell round-trip.
// Same fix as crewtimer-video-review's native/ffreader/scripts/patch-config-mak.js.
//
// Usage: node patch-config-mak.js <FFmpeg source dir>   (idempotent)
const fs = require('fs');
const path = require('path');

const ffbuild = path.join(process.argv[2], 'ffbuild');
const configMak = path.join(ffbuild, 'config.mak');

fs.writeFileSync(
  path.join(ffbuild, 'dep.awk'),
  '/including/ { sub(/^.*file: */, ""); gsub(/\\\\/, "/"); if (!match($0, / /)) print TARGET ":", $0 }\n',
);
const before = fs.readFileSync(configMak, 'utf8');
const after = before.replace(
  /awk '\/including\/[^']*'/g,
  'awk -v TARGET="$@" -f ffbuild/dep.awk',
);
fs.writeFileSync(configMak, after);
console.log(
  before === after ? 'config.mak already patched' : `Patched ${configMak}`,
);
