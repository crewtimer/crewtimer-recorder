import uuidgen from 'short-uuid';
import { HandlerResponse } from '../msgbus/MsgbusTypes';
import {
  StartRecorderMessage,
  RecorderMessage,
  GrabFrameResponse,
  RecordingStatus,
  CameraListResponse,
  DefaultRecordingProps,
  SettingsMessage,
} from './RecorderTypes';
import {
  getAddTimeOverlay,
  getGuide,
  getLoggerAlert,
  getRecordingProps,
  getReportAllGaps,
  getSystemLog,
  getWaypointList,
  setFrameGrab,
  setIsRecording,
  setLoggerAlert,
  setRecordingPropsPending,
  setRecordingStartTime,
  setSystemLog,
  setWaypointList,
} from './RecorderData';
import { showErrorDialog } from '../components/ErrorDialog';
import { ViscaMessage, ViscaMessageProps, ViscaResponse } from './ViscaTypes';
import { setViscaState } from '../visca/ViscaState';

export const startRecording = () => {
  setRecordingStartTime(Date.now());
  setRecordingPropsPending(false);
  setIsRecording(true);
  // console.log(`recording props: ${JSON.stringify(getRecordingProps())}`);
  const recordingProps = { ...DefaultRecordingProps, ...getRecordingProps() };
  recordingProps.recordingDuration = Number(recordingProps.recordingDuration);
  const cropArea = { ...recordingProps.cropArea };
  const guide = getGuide();
  return window.msgbus
    .sendMessage<StartRecorderMessage, HandlerResponse>('recorder', {
      op: 'start-recording',
      props: {
        ...recordingProps,
        cropArea,
        guide,
        reportAllGaps: getReportAllGaps(),
        addTimeOverlay: getAddTimeOverlay(),
      },
    })
    .catch((e) => {
      console.log('Error requesting start recording');
      throw e;
    });
};
export const stopRecording = () => {
  setIsRecording(false);
  setRecordingPropsPending(false);
  return window.msgbus
    .sendMessage<RecorderMessage, HandlerResponse>('recorder', {
      op: 'stop-recording',
    })
    .catch((e) => {
      console.log('Error stopping recording');
      throw e;
    });
};

export const queryRecordingStatus = () => {
  return window.msgbus
    .sendMessage<RecorderMessage, RecordingStatus>('recorder', {
      op: 'recording-status',
    })
    .catch((e) => {
      console.log('Error requesting recorder-status1');
      throw e;
    });
};

export const queryCameraList = () => {
  return window.msgbus
    .sendMessage<RecorderMessage, CameraListResponse>('recorder', {
      op: 'get-camera-list',
    })
    .catch((e) => {
      console.log('Error requesting camera list');
      throw e;
    });
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
    .catch((e) => {
      console.log('error requesting video frame');
      showErrorDialog(e);
    });
};

export const updateSettings = (props: {
  [key: string]: string | number | object;
}) => {
  return window.msgbus.sendMessage<SettingsMessage, HandlerResponse>(
    'recorder',
    {
      op: 'settings',
      props,
    },
  );
};

const viscaHandlers: { [key: string]: (reult: ViscaResponse) => void } = {};
export const sendViscaCommandToDevice = async ({
  ip,
  port,
  data,
}: ViscaMessageProps) => {
  return new Promise<ViscaResponse>((resolve, reject) => {
    // console.log(JSON.stringify({ ip, port, data }));
    const id = uuidgen.generate();
    viscaHandlers[id] = resolve;
    const props = {
      id,
      ip,
      port,
      data,
    };
    window.msgbus
      .sendMessage<ViscaMessage, ViscaResponse>('recorder', {
        op: 'send-visca-cmd',
        props,
      })
      .then((/* result */) => {
        return undefined; // the promise is completed with either resolve or reject so this is a no-op
      })
      .catch((reason) => {
        console.log('Error sending visca command ', props);
        showErrorDialog(reason);
        reject(reason);
      });
  });
};

interface SysEventMessage {
  sender: 'sysevent';
  content: {
    tsMilli: number;
    subsystem: string;
    message: string;
  };
}
interface GenericMessage {
  sender: string;
  content: any;
}
type NativeMessage = SysEventMessage | GenericMessage;

window.Util.onNativeMessage((nativeMessage: NativeMessage) => {
  const { sender, content } = nativeMessage;

  // Handle the message based on the sender and content
  switch (sender) {
    case 'mcast': {
      if (content.cmd === 'split-video' || content.cmd === 'info') {
        const wp = content.wp;
        let waypointList = getWaypointList();
        if (!waypointList.includes(wp)) {
          waypointList = [...waypointList, wp].sort((a, b) =>
            a.localeCompare(b, undefined, {
              numeric: true,
              sensitivity: 'base',
            }),
          );
          setWaypointList(waypointList);
        }
      }
      break;
    }
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

    case 'visca-state':
      {
        const { state } = content;
        setViscaState(state);
        console.log(`Visca state: ${state} `);
      }
      break;

    case 'sysevent':
      {
        console.log(content.message);
        const { message } = content;
        if (message.startsWith('Error:')) {
          setLoggerAlert(getLoggerAlert() + 1);
        }
        setSystemLog([...getSystemLog(), content]);
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
