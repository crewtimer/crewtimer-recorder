import { contextBridge, ipcRenderer } from 'electron';

contextBridge.exposeInMainWorld('msgbus', {
  sendMessage<T>(dest: string, message: T) {
    // expect a response, use invoke
    return ipcRenderer.invoke('msgbus:send', dest, message);
  },
});
