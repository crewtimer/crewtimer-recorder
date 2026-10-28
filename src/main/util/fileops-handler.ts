import {
  BrowserWindow,
  OpenDialogOptions,
  app,
  dialog,
  ipcMain,
  shell,
} from 'electron';
import { getMainWindow } from '../mainWindow';

const fs = require('fs');

ipcMain.handle('delete-file', async (_event, filename) => {
  return new Promise((resolve) => {
    fs.unlink(filename, (err: NodeJS.ErrnoException | null) => {
      if (err) {
        resolve({ error: err.message });
      } else {
        resolve({ error: '' });
      }
    });
  });
});

ipcMain.handle('open-file-dialog', async (/* event */) => {
  const result = await dialog.showOpenDialog(getMainWindow() as BrowserWindow, {
    properties: ['openFile'],
  });

  if (result.canceled) {
    return { cancelled: true, filePath: '' };
  }
  if (result.filePaths.length > 0) {
    return { cancelled: false, filePath: result.filePaths[0] };
  }
  return { cancelled: true, filePath: '' };
});

ipcMain.handle('open-dir-dialog', async (_event, title, defaultPath) => {
  const options: OpenDialogOptions = {
    title,
    defaultPath,
    properties: ['openDirectory'],
  };
  const result = await dialog.showOpenDialog(
    getMainWindow() as BrowserWindow,
    options,
  );

  if (result.canceled) {
    return { cancelled: true, path: defaultPath };
  }
  if (result.filePaths.length > 0) {
    return { cancelled: false, path: result.filePaths[0] };
  }
  return { cancelled: true, path: defaultPath };
});

ipcMain.handle('get-files-in-directory', (_event, dirPath) => {
  return new Promise((resolve) => {
    fs.readdir(
      dirPath,
      (err: NodeJS.ErrnoException | null, files: string[]) => {
        if (err) {
          resolve({ error: err.message, files: [] });
        } else {
          resolve({ error: '', files });
        }
      },
    );
  });
});

ipcMain.handle('get-documents-folder', (/* event */) => {
  return new Promise((resolve) => {
    resolve(app.getPath('documents'));
  });
});

/**
 * Shows a folder in Finder or Explorer.
 * @returns an error message to show, or '' when the folder opened
 */
async function openFileExplorer(folderPath: string): Promise<string> {
  if (!fs.existsSync(folderPath)) {
    return `The folder ${folderPath} does not exist. Check that its disk is connected, or choose another folder.`;
  }
  const error = await shell.openPath(folderPath);
  if (error) {
    console.error('Error opening file explorer:', error);
    return `Could not open ${folderPath}: ${error}`;
  }
  return '';
}

ipcMain.handle('open-file-explorer', (_event, dirPath) =>
  openFileExplorer(dirPath),
);

// applog.txt and applog.prev.txt are written to the user data folder
ipcMain.handle('open-log-folder', () =>
  openFileExplorer(app.getPath('userData')),
);
