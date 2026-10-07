import React from 'react';
import { Button, Typography } from '@mui/material';
import FiberManualRecordIcon from '@mui/icons-material/FiberManualRecord';
import StopIcon from '@mui/icons-material/Stop';
import {
  getCameraTimeSample,
  getRecordingProps,
  useIsRecording,
  useRecordingProps,
} from '../recorder/RecorderData';
import { useCameraList } from '../recorder/CameraMonitor';
import { startRecording, stopRecording } from '../recorder/RecorderApi';
import { showErrorDialog } from './ErrorDialog';
import { showCameraTimeDialog } from '../recorder/CameraTimeDialog';
import { hasCameraTimeMismatch } from '../recorder/CameraTime';
import {
  CAMERA_FALLBACK_IP,
  showCameraFallbackDialog,
} from '../recorder/CameraFallbackDialog';

export const StartButton: React.FC = () => {
  const [isRecording] = useIsRecording();

  const [recordingProps] = useRecordingProps();
  const [cameraList] = useCameraList();
  const camFound = cameraList.some(
    (c) => c.name === recordingProps.networkCamera,
  );

  const handleToggleRecording = () => {
    if (isRecording) {
      stopRecording().catch(showErrorDialog);
      return;
    }
    const record = () => {
      startRecording().catch(showErrorDialog);
    };
    const checkCameraTime = () => {
      const sample = getCameraTimeSample();
      const currentRecordingProps = getRecordingProps();
      if (
        sample?.camera === currentRecordingProps.networkCamera &&
        sample?.protocol === currentRecordingProps.protocol &&
        hasCameraTimeMismatch(sample.cameraTime, sample.systemTime)
      ) {
        showCameraTimeDialog(record);
      } else {
        record();
      }
    };
    const camera = cameraList.find(
      (c) => c.name === recordingProps.networkCamera,
    );
    if (camera?.address === CAMERA_FALLBACK_IP) {
      showCameraFallbackDialog(checkCameraTime);
    } else {
      checkCameraTime();
    }
  };

  const blockedReason =
    !isRecording && !camFound
      ? `Can't start: ${recordingProps.networkCamera ? 'camera not found' : 'no camera selected'}`
      : '';

  return (
    <>
      {blockedReason && (
        <Typography
          id="start-blocked-reason"
          color="warning.main"
          sx={{ fontSize: 13, whiteSpace: 'nowrap' }}
        >
          {blockedReason}
        </Typography>
      )}
      <Button
        variant="contained"
        color={isRecording ? 'error' : 'success'}
        disabled={!!blockedReason}
        aria-describedby={blockedReason ? 'start-blocked-reason' : undefined}
        onClick={handleToggleRecording}
        startIcon={isRecording ? <StopIcon /> : <FiberManualRecordIcon />}
        sx={{ height: 44, px: 2.5, whiteSpace: 'nowrap', flexShrink: 0 }}
      >
        {isRecording ? 'Stop recording' : 'Start recording'}
      </Button>
    </>
  );
};
