import React, { useEffect } from 'react';
import { Stack, Typography } from '@mui/material';
import RecordIcon from '@mui/icons-material/FiberManualRecord';
import {
  DefaultRecordingStatus,
  queryRecordingStatus,
  setIsRecording,
  setRecordingStatus,
  useRecordingStatus,
} from './RecorderApi';

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
    .catch((err) => console.error(err));
};

const RecordingStatus: React.FC = () => {
  const [recordingStatus] = useRecordingStatus();
  const isRecording = recordingStatus.recording;
  const seconds = formatTime(recordingStatus.recordingDuration);

  useEffect(() => {
    let interval: NodeJS.Timeout | null = null;
    interval = setInterval(checkStatus, 1000);

    return () => {
      if (interval) clearInterval(interval);
    };
  }, []);

  return isRecording ? (
    <Stack direction="column">
      <Stack direction="row" sx={{ alignItems: 'center' }}>
        <RecordIcon style={{ color: '#ff0000' }} />
        <Typography variant="body2">{`${seconds}`}</Typography>
      </Stack>
      {recordingStatus.frameProcessor.filename ? (
        <>
          <Typography variant="body2">{`${recordingStatus.frameProcessor.filename}`}</Typography>
          <Typography variant="body2">{`${recordingStatus.frameProcessor.width}x${recordingStatus.frameProcessor.height} ${recordingStatus.frameProcessor.fps} fps`}</Typography>
        </>
      ) : null}
    </Stack>
  ) : null;
};

export default RecordingStatus;