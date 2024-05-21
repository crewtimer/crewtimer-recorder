import { contextBridge, ipcRenderer } from 'electron';

contextBridge.exposeInMainWorld('msgbus', {
  sendMessage<T>(dest: string, message: T) {
    // expect a response, use invoke
    // console.log('sendMessage', dest, message);
    // return new Promise<any>((resolve) => {
    //   resolve({ status: 'OK' });
    // });
    return ipcRenderer.invoke('msgbus:send', dest, message);
  },
  // onMessageRx: (
  //   dest: string,
  //   callback: (_event: IpcRendererEvent, dest: string, value: unknown) => void,
  // ) => ipcRenderer.on('msgbus:rx', callback),
});
