import { useEffect } from 'react';
import { UseDatum } from 'react-usedatum';
import { queryCameraList } from './RecorderApi';
import { showErrorDialog } from '../components/ErrorDialog';
import { useIsRecording, useRecordingProps } from './RecorderData';
import { setViscaIP, useViscaIP } from '../visca/ViscaState';
import { sendViscaCommand } from '../visca/ViscaAPI';

export const [useCameraList, setCameraList] = UseDatum<
  { name: string; address: string }[]
>([]);

export const CameraMonitor = () => {
  const [isRecording] = useIsRecording();
  const [viscaIP] = useViscaIP();
  const [recordingProps] = useRecordingProps();

  /** Periodically query for the available NDI cameras */
  useEffect(() => {
    if (isRecording) {
      return () => {};
    }
    const monitor = async () => {
      queryCameraList()
        .then((result) => {
          setCameraList(result?.cameras || []);
          return null;
        })
        .catch(showErrorDialog);
    };
    monitor();
    const timer = setInterval(monitor, 5000);
    return () => clearInterval(timer);
  }, [isRecording]);

  /** Periodically send a VISCA Command to detect camera connection issues */
  useEffect(() => {
    if (viscaIP !== recordingProps.networkIP) {
      setViscaIP(recordingProps.networkIP);
      return undefined;
    }
    if (!viscaIP) {
      return undefined;
    }

    // const monitor = () => {
    //   sendViscaCommand({ type: 'AUTO_FOCUS_VALUE' })
    //     .then(() => {
    //       return true;
    //     })
    //     .catch(() => {});
    // };
    // monitor();
    // const timer = setInterval(monitor, 5000);
    // return () => clearInterval(timer);
    return undefined;
  }, [recordingProps.networkIP, viscaIP]);

  // eslint-disable-next-line react/jsx-no-useless-fragment
  return <></>;
};
export default CameraMonitor;
