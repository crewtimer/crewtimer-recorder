/* eslint global-require: off, no-console: off, promise/always-return: off */

/**
 * This module executes inside of electron's main process. You can start
 * electron renderer process from here and communicate with the other processes
 * through IPC.
 *
 * When running `npm run build` or `npm run build:main`, this file is compiled to
 * `./src/main.js` using webpack. This gives us some performance wins.
 */
import path from 'path';
import { app, BrowserWindow, shell } from 'electron';
import { setLogFile, setNativeMessageCallback } from 'crewtimer_video_recorder';
import MenuBuilder from './menu';
import { resolveHtmlPath } from './util';
import './store/store';
import './msgbus/msgbus-main';
import { stopRecorder, initRecorder } from './recorder/recorder-main';
import { setMainWindow } from './mainWindow';
import './util/fileops-handler';

let mainWindow: BrowserWindow | null = null;

if (process.env.NODE_ENV === 'production') {
  const sourceMapSupport = require('source-map-support');
  sourceMapSupport.install();
}

const isDebug =
  process.env.NODE_ENV === 'development' || process.env.DEBUG_PROD === 'true';

if (isDebug) {
  require('electron-debug')();
}

// Create a write stream (in append mode)
const logFilePath = path.join(app.getPath('userData'), 'applog.txt');
console.log(`Logging to ${logFilePath}`);
const logStream = require('fs').createWriteStream(logFilePath);

// Redirect console.log to the log file
console.log = (...args) => {
  const msg = `${args.map((x) => String(x)).join(' ')}\n`;
  logStream.write(msg);
  process.stdout.write(msg);
};

// Redirect console.error to the log file
console.error = (...args) => {
  const msg = `${args.map((x) => String(x)).join(' ')}\n`;
  logStream.write(msg);
  process.stderr.write(msg);
};

setLogFile(logFilePath);
console.log('Starting app...');

const installExtensions = async () => {
  const installer = require('electron-devtools-installer');
  const forceDownload = !!process.env.UPGRADE_EXTENSIONS;
  const extensions = ['REACT_DEVELOPER_TOOLS'];

  return installer
    .default(
      extensions.map((name) => installer[name]),
      forceDownload,
    )
    .catch(console.log);
};

const createWindow = async () => {
  if (isDebug) {
    await installExtensions();
  }

  const RESOURCES_PATH = app.isPackaged
    ? path.join(process.resourcesPath, 'assets')
    : path.join(__dirname, '../../assets');

  const getAssetPath = (...paths: string[]): string => {
    return path.join(RESOURCES_PATH, ...paths);
  };

  mainWindow = new BrowserWindow({
    show: false,
    width: 1024,
    height: 728,
    icon: getAssetPath('icon.png'),
    webPreferences: {
      preload: app.isPackaged
        ? path.join(__dirname, 'preload.js')
        : path.join(__dirname, '../../.erb/dll/preload.js'),
    },
  });

  setMainWindow(mainWindow);

  mainWindow.loadURL(resolveHtmlPath('index.html'));

  mainWindow.on('ready-to-show', () => {
    if (!mainWindow) {
      throw new Error('"mainWindow" is not defined');
    }
    initRecorder();
    if (process.env.START_MINIMIZED) {
      mainWindow.minimize();
    } else {
      mainWindow.show();
    }
  });

  // startRecording();

  process.on('uncaughtException', (error) => {
    console.error('Uncaught Exception:', error);
  });

  mainWindow.on('closed', () => {
    mainWindow = null;
    setMainWindow(null);
  });

  const menuBuilder = new MenuBuilder(mainWindow);
  menuBuilder.buildMenu();

  // Open urls in the user's browser
  mainWindow.webContents.setWindowOpenHandler((edata) => {
    shell.openExternal(edata.url);
    return { action: 'deny' };
  });

  // Remove this if your app does not use auto updates
  // eslint-disable-next-line
  // new AppUpdater();
};

/**
 * Add event listeners...
 */

app.on('window-all-closed', () => {
  // Respect the OSX convention of having the application in memory even
  // after all windows have been closed
  // Nope, it's a pain for the most part. (Glenn)
  // if (process.platform !== 'darwin') {
  //   app.quit();
  // }
  console.log('Window all closed handler calling stopRecorder');
  stopRecorder();
  console.log('Recording stopped, app quit pending');
  app.quit();
});

app
  .whenReady()
  .then(() => {
    createWindow();
    app.on('activate', () => {
      // On macOS it's common to re-create a window in the app when the
      // dock icon is clicked and there are no other windows open.
      if (mainWindow === null) createWindow();
    });
  })
  .catch(console.log);

app.on('ready', () => {
  setNativeMessageCallback((message) => {
    // Forward the message to the renderer
    mainWindow?.webContents.send('native-message', message);
  });
});
