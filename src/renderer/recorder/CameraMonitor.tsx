import { useEffect } from 'react';
import { UseDatum } from 'react-usedatum';
import { queryCameraList } from './RecorderApi';
import { showErrorDialog } from '../components/ErrorDialog';
import { useIsRecording, useRecordingProps } from './RecorderData';
import { useViscaIP } from '../visca/ViscaState';

export const [useCameraList, setCameraList] = UseDatum<
  { name: string; address: string }[]
>([]);

export const CameraMonitor = () => {
  const [isRecording] = useIsRecording();
  const [recordingProps] = useRecordingProps();
  const [cameraList] = useCameraList();
  const [, setViscaIP] = useViscaIP();

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

  useEffect(() => {
    // Keep the camera IP address in sync with the selected camera
    const camera = cameraList.find(
      (c) => c.name === recordingProps.networkCamera,
    );
    if (camera) {
      setViscaIP(camera.address);
    }
  }, [cameraList, recordingProps.networkCamera, setViscaIP]);

  /** Periodically send a VISCA Command to detect camera connection issues */
  // useEffect(() => {
  //   // const monitor = () => {
  //   //   sendViscaCommand({ type: 'AUTO_FOCUS_VALUE' })
  //   //     .then(() => {
  //   //       return true;
  //   //     })
  //   //     .catch(() => {});
  //   // };
  //   // monitor();
  //   // const timer = setInterval(monitor, 5000);
  //   // return () => clearInterval(timer);
  //   return undefined;
  // }, [viscaIP]);

  // eslint-disable-next-line react/jsx-no-useless-fragment
  return <></>;
};
export default CameraMonitor;
