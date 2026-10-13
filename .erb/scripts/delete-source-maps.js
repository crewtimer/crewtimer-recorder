import fs from 'fs';
import path from 'path';
import webpackPaths from '../configs/webpack.paths';

function deleteJsSourceMaps(dir) {
  fs.readdirSync(dir)
    .filter((entry) => entry.endsWith('.js.map'))
    .forEach((entry) => fs.rmSync(path.join(dir, entry), { force: true }));
}

export default function deleteSourceMaps() {
  if (fs.existsSync(webpackPaths.distMainPath)) {
    deleteJsSourceMaps(webpackPaths.distMainPath);
  }
  if (fs.existsSync(webpackPaths.distRendererPath)) {
    deleteJsSourceMaps(webpackPaths.distRendererPath);
  }
}
