import uuidgen from 'short-uuid';
import { HandlerResponse } from '../msgbus/MsgbusTypes';
import {
  StartRecorderMessage,
  RecorderMessage,
  RecordingLog,
  GrabFrameResponse,
  RecordingStatus,
  CameraListResponse,
  DefaultRecordingProps,
} from './RecorderTypes';
import {
  getGuide,
  getRecordingProps,
  setFrameGrab,
  setIsRecording,
  setRecordingStartTime,
} from './RecorderData';
import { showErrorDialog } from '../components/ErrorDialog';
import { ViscaMessage, ViscaMessageProps, ViscaResponse } from './ViscaTypes';

export const startRecording = () => {
  setRecordingStartTime(Date.now());
  setIsRecording(true);
  // console.log(`recording props: ${JSON.stringify(getRecordingProps())}`);
  const recordingProps = { ...DefaultRecordingProps, ...getRecordingProps() };
  recordingProps.recordingDuration = Number(recordingProps.recordingDuration);
  const cropArea = { ...recordingProps.cropArea };
  const guide = getGuide();
  return window.msgbus.sendMessage<StartRecorderMessage, HandlerResponse>(
    'recorder',
    {
      op: 'start-recording',
      props: { ...recordingProps, cropArea, guide },
    },
  );
};
export const stopRecording = () => {
  setIsRecording(false);
  return window.msgbus.sendMessage<RecorderMessage, HandlerResponse>(
    'recorder',
    {
      op: 'stop-recording',
    },
  );
};

export const queryRecordingStatus = () => {
  return window.msgbus.sendMessage<RecorderMessage, RecordingStatus>(
    'recorder',
    {
      op: 'recording-status',
    },
  );
};

export const queryRecordingLog = () => {
  return window.msgbus.sendMessage<RecorderMessage, RecordingLog>('recorder', {
    op: 'recording-log',
  });
};

export const queryCameraList = () => {
  return window.msgbus.sendMessage<RecorderMessage, CameraListResponse>(
    'recorder',
    {
      op: 'get-camera-list',
    },
  );
};

export const requestVideoFrame = async () => {
  window.msgbus
    .sendMessage<RecorderMessage, GrabFrameResponse>('recorder', {
      op: 'grab-frame',
    })
    .then((frame) => {
      if (frame.status === 'OK' && frame.data) {
        setFrameGrab(frame);
      }
      // else {
      //   setFrameGrab(undefined);
      // }
      return frame;
    })
    .catch(showErrorDialog);
};

const viscaHandlers: { [key: string]: (reult: ViscaResponse) => void } = {};
export const sendViscaCommandToDevice = async ({
  ip,
  port,
  data,
}: ViscaMessageProps) => {
  return new Promise((resolve, reject) => {
    const id = uuidgen.generate();
    viscaHandlers[id] = resolve;
    window.msgbus
      .sendMessage<ViscaMessage, ViscaResponse>('recorder', {
        op: 'send-visca-cmd',
        props: {
          id,
          ip,
          port,
          data,
        },
      })
      .then((result) => {
        if (result.status === 'OK') {
          console.log('visca sent OK');
        }
        return undefined;
      })
      .catch((reason) => {
        showErrorDialog(reason);
        reject(reason);
      });
  });
};

interface NativeMessage {
  sender: string;
  content: any;
}

window.Util.onNativeMessage((message: NativeMessage) => {
  const { sender, content } = message;

  // Handle the message based on the sender and content
  switch (sender) {
    case 'visca-status':
      {
        const statusMessage = content.msg;
        console.log(statusMessage);
        // Update the UI or handle the status change
      }
      break;
    // case 'system':
    //   const errorMessage = content;
    //   console.error('SYSTEM ERROR:', errorMessage);
    //   break;
    case 'visca-result':
      {
        const { id } = content;
        const resolve = viscaHandlers[id];
        if (resolve) {
          resolve(content);
          delete viscaHandlers[id];
        }
      }
      break;
    // case 'VID':
    //   const videoMessage = content;
    //   console.log('VIDEO MESSAGE:', videoMessage);
    //   break;
    default:
      console.warn(`Unhandled message sender: ${sender}`);
      break;
  }
});
