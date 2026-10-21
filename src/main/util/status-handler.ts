/**
 * Recorder health reporting for the renderer: multicasts status and events on
 * the CrewTimer group the native recorder already listens on for split-video
 * commands, and reports disk usage of the recording folder.
 */
import dgram from 'dgram';
import os from 'os';
import path from 'path';
import { execFile } from 'child_process';
import { statfs } from 'fs/promises';
import { ipcMain } from 'electron';
import { getStoredValue } from '../store/store';

const MCAST_ADDR = '239.215.23.42';
const MCAST_PORT = 52342;

let socket: dgram.Socket | undefined;
const getSocket = () => {
  if (!socket) {
    socket = dgram.createSocket({ type: 'udp4', reuseAddr: true });
    socket.on('error', (err) => {
      console.error('Multicast send socket error:', err);
      socket?.close();
      socket = undefined;
    });
  }
  return socket;
};

const multicastSend = (msg: object, done?: () => void) => {
  // The host name tells apart several recorders bound to the same waypoint
  const data = JSON.stringify({ ...msg, host: os.hostname() });
  getSocket().send(data, MCAST_PORT, MCAST_ADDR, (err) => {
    if (err) console.error('Multicast send failed:', err);
    done?.();
  });
};

ipcMain.handle('mcast-send', (_event, msg: object) => multicastSend(msg));

/**
 * Tells listeners this recorder is exiting.  Resolves once sent, or after a
 * short timeout so a network problem cannot hold up the quit.
 */
export const sendRecorderExit = () =>
  new Promise<void>((resolve) => {
    const timer = setTimeout(resolve, 500);
    const { waypoint } = getStoredValue<{ waypoint?: string }>('record', {});
    multicastSend(
      { cmd: 'recorder-exit', wp: waypoint ?? '', tsMilli: Date.now() },
      () => {
        clearTimeout(timer);
        resolve();
      },
    );
  });

/**
 * On Windows statfs uses GetDiskFreeSpaceW, which can reject a folder that is
 * not a drive root, so retry on the root of the folder's drive or share.
 */
const statfsWithRootFallback = async (folder: string) => {
  try {
    return await statfs(folder);
  } catch (err) {
    const { root } = path.parse(path.resolve(folder));
    if (process.platform !== 'win32' || !root) throw err;
    return statfs(root);
  }
};

// Asks Foundation for the capacity available for important usage, as Finder
// shows it, via JavaScript for Automation so no native code is needed
const MAC_IMPORTANT_FREE_SCRIPT = `ObjC.import('Foundation');
function run(argv) {
  const value = Ref();
  $.NSURL.fileURLWithPath(argv[0]).getResourceValueForKeyError(
    value, $.NSURLVolumeAvailableCapacityForImportantUsageKey, null);
  return ObjC.unwrap(value[0]);
}`;

/**
 * On APFS, statfs leaves out purgeable space such as local Time Machine
 * snapshots, which macOS frees as the disk fills, so it can report a fraction
 * of what Finder does.  Resolves undefined if the query fails.
 */
const macImportantFreeBytes = (folder: string) =>
  new Promise<number | undefined>((resolve) => {
    execFile(
      'osascript',
      ['-l', 'JavaScript', '-e', MAC_IMPORTANT_FREE_SCRIPT, folder],
      { timeout: 5000 },
      (err, stdout) => {
        const bytes = Number(stdout.trim());
        resolve(!err && bytes > 0 ? bytes : undefined);
      },
    );
  });

// The renderer polls, so log a failure once rather than on every poll
let lastDiskError = '';

ipcMain.handle('get-disk-usage', async (_event, folder: string) => {
  try {
    const stats = await statfsWithRootFallback(folder);
    const totalBytes = stats.blocks * stats.bsize;
    // bavail is what this user can still write; root-reserved space counts as used
    let freeBytes = stats.bavail * stats.bsize;
    if (process.platform === 'darwin') {
      const importantFree = await macImportantFreeBytes(folder);
      if (importantFree) freeBytes = Math.min(totalBytes, importantFree);
    }
    lastDiskError = '';
    return {
      error: '',
      totalBytes,
      freeBytes,
      usedPercent: totalBytes
        ? (100 * (totalBytes - freeBytes)) / totalBytes
        : 0,
    };
  } catch (err) {
    const error = String(err);
    if (error !== lastDiskError) {
      console.error(`Disk usage of ${folder} unavailable:`, err);
      lastDiskError = error;
    }
    return { error, totalBytes: 0, freeBytes: 0, usedPercent: 0 };
  }
});
