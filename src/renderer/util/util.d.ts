import { IpcRendererEvent } from 'electron';
import { CloseFileReturn, OpenDirReturn } from '../../main/util/util-preload';

export interface OpenFileReturn {
  cancelled: boolean;
  filePath: string;
}

export interface DirListReturn {
  error: string;
  files: string[];
}
declare global {
  interface Window {
    Util: {
      onUserMessage(
        callback: (event: IpcRendererEvent, level: string, msg: string) => void,
      ): void;
      openFileDialog(): Promise<OpenFileReturn>;
      openDirDialog(title: string, defaultPath: string): Promise<OpenDirReturn>;
      openFileExplorer(path: string): Promise<void>;
      getFilesInDirectory(dirPath: string): Promise<DirListReturn>;
      deleteFile(filename: string): Promise<CloseFileReturn>;
      getDocumentsFolder(): Promise<string>;
      onNativeMessage(
        callback: (message: { sender: string; content: any }) => void,
      ): void;
    };
    platform: {
      platform: string;
      pathSeparator: string;
      appVersion: string;
    };
  }
}

export {};
