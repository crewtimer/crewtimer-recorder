import React from 'react';
import { Tooltip, Button } from '@mui/material';
import PlayArrowIcon from '@mui/icons-material/PlayArrow';
import StopIcon from '@mui/icons-material/Stop';
import { useIsRecording, useRecordingProps } from '../recorder/RecorderData';
import { useCameraList } from '../recorder/CameraMonitor';
import { startRecording, stopRecording } from '../recorder/RecorderApi';
import { showErrorDialog } from './ErrorDialog';

interface StartButtonProps {
  hideStopButton?: boolean;
}
export const StartButton: React.FC<StartButtonProps> = ({ hideStopButton }) => {
  const [isRecording] = useIsRecording();

  const [recordingProps] = useRecordingProps();
  const [cameraList] = useCameraList();
  const camFound = cameraList.some(
    (c) => c.name === recordingProps.networkCamera,
  );

  const handleToggleRecording = () => {
    if (isRecording) {
      stopRecording().catch(showErrorDialog);
    } else {
      startRecording().catch(showErrorDialog);
    }
  };

  const canToggleRecording = camFound;

  return !isRecording || !hideStopButton ? (
    <Tooltip title={isRecording ? 'Stop Recording' : 'Start Recording'}>
      {/* span provides child when button disabled */}
      <span
        role="button"
        tabIndex={canToggleRecording ? 0 : -1}
        onClick={canToggleRecording ? handleToggleRecording : undefined}
        onKeyDown={(event) => {
          if (
            canToggleRecording &&
            (event.key === 'Enter' || event.key === ' ')
          ) {
            event.preventDefault();
            handleToggleRecording();
          }
        }}
        style={{
          display: 'inline-flex',
          cursor: canToggleRecording ? 'pointer' : 'default',
        }}
      >
        <Button
          variant="contained"
          disabled={!canToggleRecording}
          tabIndex={-1}
          startIcon={isRecording ? <StopIcon /> : <PlayArrowIcon />}
          sx={{
            backgroundColor: isRecording ? 'red' : 'green',
            pointerEvents: 'none',
            '&:hover': {
              backgroundColor: isRecording ? 'darkred' : 'darkgreen',
            },
          }}
        >
          {isRecording ? 'Stop' : 'Start'}
        </Button>
      </span>
    </Tooltip>
  ) : (
    // eslint-disable-next-line react/jsx-no-useless-fragment
    <></>
  );
};
