import { ipcMain } from 'electron';
import { msgbus } from './msgbus-util';

export type OnMsgChangeHandler = (dest: string, message: any) => void;

const changeHandlers = new Map<string, OnMsgChangeHandler>();
export function publish(dest: string, message: any) {
  const handler = changeHandlers.get(dest);
  if (handler) {
    handler(dest, message);
  }
}

export const onPublishChange = (dest: string, handler: OnMsgChangeHandler) => {
  changeHandlers.set(dest, handler);
};

ipcMain.handle('msgbus:send', (_event, dest: string, msg: any) => {
  try {
    const ret = msgbus.sendMessage(dest, msg);
    return ret;
  } catch (err) {
    return { status: `${err instanceof Error ? err.message : err}` };
  }
});
