/**
 * Recorder health reporting for the renderer: multicasts status and events on
 * the CrewTimer group the native recorder already listens on for split-video
 * commands, and reports disk usage of the recording folder.
 */
import dgram from 'dgram';
import os from 'os';
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

ipcMain.handle('get-disk-usage', async (_event, folder: string) => {
  try {
    const stats = await statfs(folder);
    const totalBytes = stats.blocks * stats.bsize;
    // bavail is what this user can still write; root-reserved space counts as used
    const freeBytes = stats.bavail * stats.bsize;
    return {
      error: '',
      totalBytes,
      freeBytes,
      usedPercent: totalBytes
        ? (100 * (totalBytes - freeBytes)) / totalBytes
        : 0,
    };
  } catch (err) {
    return { error: String(err), totalBytes: 0, freeBytes: 0, usedPercent: 0 };
  }
});
