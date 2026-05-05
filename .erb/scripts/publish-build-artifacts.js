import fs from 'fs';
import os from 'os';
import path from 'path';
import { spawnSync } from 'child_process';

const projectRoot = process.cwd();
const buildDir = path.join(projectRoot, 'release', 'build');
const appPackagePath = path.join(projectRoot, 'release', 'app', 'package.json');

const appPackage = JSON.parse(fs.readFileSync(appPackagePath, 'utf8'));
const tag = `v${appPackage.version}-app`;
const artifacts = fs
  .readdirSync(buildDir)
  .filter((filename) => /\.(dmg|exe)$/i.test(filename))
  .map((filename) => path.join(buildDir, filename));

const getPrebuildToken = () => {
  const prebuildrc = path.join(os.homedir(), '.prebuildrc');
  if (!fs.existsSync(prebuildrc)) {
    return undefined;
  }

  const entries = fs
    .readFileSync(prebuildrc, 'utf8')
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line && !line.startsWith('#'))
    .map((line) => {
      const separator = line.indexOf('=');
      if (separator === -1) {
        return undefined;
      }
      return [
        line.slice(0, separator).trim(),
        line.slice(separator + 1).trim(),
      ];
    })
    .filter(Boolean);

  return entries.find(([key]) => key === 'token')?.[1];
};

const ghEnv = {
  ...process.env,
  GH_TOKEN: process.env.GH_TOKEN || getPrebuildToken(),
};

const run = (command, args, options = {}) => {
  const result = spawnSync(command, args, {
    stdio: 'inherit',
    env: ghEnv,
    ...options,
  });

  if (result.error) {
    throw result.error;
  }

  return result.status ?? 0;
};

if (artifacts.length === 0) {
  throw new Error(`No .dmg or .exe files found in ${buildDir}`);
}

if (run('gh', ['release', 'view', tag], { stdio: 'ignore' }) === 0) {
  run('gh', ['release', 'upload', tag, ...artifacts, '--clobber']);
} else {
  run('gh', [
    'release',
    'create',
    tag,
    ...artifacts,
    '--title',
    tag,
    '--notes',
    `CrewTimer Video Recorder ${appPackage.version}`,
  ]);
}
