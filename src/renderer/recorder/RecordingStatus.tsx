import React, { useEffect } from 'react';
import { Stack, Typography } from '@mui/material';
import RecordIcon from '@mui/icons-material/FiberManualRecord';
import { queryRecordingStatus, stopRecording } from './RecorderApi';
import {
  setRecordingStatus,
  setIsRecording,
  useRecordingStatus,
  getRecordingProps,
} from './RecorderData';
import { DefaultRecordingStatus } from './RecorderTypes';
import { showErrorDialog } from '../components/ErrorDialog';

const formatTime = (totalSeconds: number) => {
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const secs = totalSeconds % 60;
  return [hours, minutes, secs].map((v) => (v < 10 ? `0${v}` : v)).join(':');
};

const checkStatus = () => {
  queryRecordingStatus()
    .then((result) => {
      // console.log(JSON.stringify(result));
      const status = { ...DefaultRecordingStatus, ...result };
      setRecordingStatus(status);
      setIsRecording(status.recording);
      return result;
    })
    .catch(showErrorDialog);
};

const RecordingStatus: React.FC = () => {
  const [recordingStatus] = useRecordingStatus();
  const { cropArea } = getRecordingProps();
  const isRecording = recordingStatus.recording;
  const seconds = formatTime(recordingStatus.recordingDuration);

  useEffect(() => {
    let interval: NodeJS.Timeout | null = null;
    interval = setInterval(checkStatus, 1000);

    return () => {
      if (interval) clearInterval(interval);
    };
  }, []);

  let cropText = '';
  if (cropArea.width != 1 || cropArea.height != 1) {
    cropText = ` -> ${Math.round((cropArea.width * recordingStatus.frameProcessor.width) / 4) * 4}x${Math.round((cropArea.height * recordingStatus.frameProcessor.height) / 4) * 4}`;
  }

  return isRecording ? (
    <Stack direction="column">
      <Stack
        direction="row"
        sx={{ alignItems: 'center' }}
        onClick={stopRecording}
      >
        <RecordIcon style={{ color: '#ff0000' }} />
        <Typography variant="body2">{`${seconds}`}</Typography>
      </Stack>
      {recordingStatus.frameProcessor.filename ? (
        <>
          <Typography
            sx={{ fontSize: 12 }}
          >{`${recordingStatus.frameProcessor.filename}`}</Typography>
          <Typography
            sx={{ fontSize: 11 }}
          >{`${recordingStatus.frameProcessor.width}x${recordingStatus.frameProcessor.height}${cropText} ${recordingStatus.frameProcessor.fps} fps`}</Typography>
        </>
      ) : null}
    </Stack>
  ) : null;
};

export default RecordingStatus;
