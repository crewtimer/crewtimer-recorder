import { useEffect } from 'react';
import { UseDatum } from 'react-usedatum';
import { queryCameraList } from './RecorderApi';
import { showErrorDialog } from '../components/ErrorDialog';
import { useIsRecording, useRecordingProps } from './RecorderData';
import { useViscaIP } from '../visca/ViscaState';

export const [useCameraList, setCameraList] = UseDatum<
  { name: string; address: string }[]
>([]);

export const [useInitialCameraSearchComplete, setInitialCameraSearchComplete] =
  UseDatum(false);

/** Show "not found" after this long even if the native side never reports a completed scan. */
const MAX_INITIAL_SEARCH_MS = 12000;

/**
 * Query the native camera list.
 * @returns true if a requested rescan is still running.
 */
export const refreshCameraList = (rescan = false) =>
  queryCameraList(rescan)
    .then((result) => {
      setCameraList(result?.cameras || []);
      // Treat errors as complete so the UI never waits on a search that
      // won't report back.
      if (result?.status !== 'OK' || result.initialScanComplete) {
        setInitialCameraSearchComplete(true);
      }
      return result?.status === 'OK' && result.rescanInProgress;
    })
    .catch((e) => {
      setInitialCameraSearchComplete(true);
      showErrorDialog(e);
      return false;
    });

export const CameraMonitor = () => {
  const [isRecording] = useIsRecording();
  const [recordingProps] = useRecordingProps();
  const [cameraList] = useCameraList();
  const [, setViscaIP] = useViscaIP();

  useEffect(() => {
    const timer = setTimeout(
      () => setInitialCameraSearchComplete(true),
      MAX_INITIAL_SEARCH_MS,
    );
    return () => clearTimeout(timer);
  }, []);

  /** Periodically query for the available NDI cameras */
  useEffect(() => {
    if (isRecording) {
      return () => {};
    }
    refreshCameraList();
    const timer = setInterval(() => refreshCameraList(), 5000);
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
