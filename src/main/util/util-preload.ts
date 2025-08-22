/**
 * Support for using LapStorage in renderer
 *
 * Add ```import './util/util-preload';``` to preload.ts to integrate with main
 */
import { contextBridge, ipcRenderer, IpcRendererEvent } from 'electron';

export interface CloseFileReturn {
  error: string;
}

export interface OpenFileReturn {
  cancelled: boolean;
  filePath: string;
}

export interface OpenDirReturn {
  cancelled: boolean;
  path: string;
}

export interface DirListReturn {
  error: string;
  files: string[];
}
// Function to open the file dialog and return the selected file path as a promise
export function openFileDialog(): Promise<OpenFileReturn> {
  return new Promise((resolve) => {
    ipcRenderer
      .invoke('open-file-dialog')
      .then((result) => resolve(result))
      .catch(() => resolve({ cancelled: true, filePath: '' }));
  });
}

export function deleteFile(filename: string): Promise<CloseFileReturn> {
  return new Promise((resolve) => {
    ipcRenderer
      .invoke('delete-file', filename)
      .then((result) => resolve(result))
      .catch((err) => resolve({ error: String(err) }));
  });
}

export function openDirDialog(
  title: string,
  defaultPath: string,
): Promise<OpenDirReturn> {
  return new Promise((resolve) => {
    ipcRenderer
      .invoke('open-dir-dialog', title, defaultPath)
      .then((result) => resolve(result))
      .catch(() => resolve({ cancelled: true, path: defaultPath }));
  });
}

export function openFileExplorer(path: string): Promise<void> {
  return new Promise((resolve) => {
    ipcRenderer
      .invoke('open-file-explorer', path)
      .then((result) => resolve(result))
      .catch(() => resolve());
  });
}

// Function to get the files in a directory and return them as a promise
export function getFilesInDirectory(dirPath: string): Promise<DirListReturn> {
  // console.log('Executing getFiles in dir preload');
  return new Promise((resolve) => {
    ipcRenderer
      .invoke('get-files-in-directory', dirPath)
      .then((result: DirListReturn) => {
        // console.log('get files in dir result=' + JSON.stringify(result));
        return resolve(result);
      })
      .catch((err) => ({ error: String(err), files: [] }));
  });
}

export function getDocumentsFolder(): Promise<string> {
  return new Promise((resolve) => {
    ipcRenderer
      .invoke('get-documents-folder')
      .then((result) => resolve(result))
      .catch(() => resolve(''));
  });
}

contextBridge.exposeInMainWorld('Util', {
  onUserMessage: (
    callback: (_event: IpcRendererEvent, level: string, msg: string) => void,
  ) => ipcRenderer.on('user-message', callback),
  getFilesInDirectory,
  openFileDialog,
  openDirDialog,
  openFileExplorer,
  deleteFile,
  getDocumentsFolder,
  onNativeMessage: (
    callback: (message: { sender: string; content: any }) => void,
  ) => ipcRenderer.on('native-message', (event, message) => callback(message)),
});

const appVersion = require('../../../release/app/package.json').version;

contextBridge.exposeInMainWorld('platform', {
  platform: process.platform,
  pathSeparator: process.platform.includes('win') ? '\\' : '/',
  appVersion,
});
