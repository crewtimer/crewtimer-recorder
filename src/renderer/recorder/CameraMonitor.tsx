import { useEffect } from 'react';
import { UseDatum } from 'react-usedatum';
import { queryCameraList } from './RecorderApi';
import { showErrorDialog } from '../components/ErrorDialog';
import { useIsRecording } from './RecorderData';

export const [useCameraList, setCameraList] = UseDatum<
  { name: string; address: string }[]
>([]);

export const CameraMonitor = () => {
  const [isRecording] = useIsRecording();
  useEffect(() => {
    if (isRecording) {
      return () => {};
    }
    const timer = setInterval(() => {
      queryCameraList()
        .then((result) => {
          setCameraList(result?.cameras || []);
          return null;
        })
        .catch(showErrorDialog);
    }, 5000);
    return () => clearInterval(timer);
  }, [isRecording]);
  // eslint-disable-next-line react/jsx-no-useless-fragment
  return <></>;
};
export default CameraMonitor;
