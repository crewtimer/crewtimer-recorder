import React, { useEffect } from 'react';
import { Box, LinearProgress, Stack, Typography } from '@mui/material';
import { alpha } from '@mui/material/styles';
import { queryRecordingStatus } from './RecorderApi';
import {
  setRecordingStatus,
  setIsRecording,
  useRecordingStatus,
  useRecordingProps,
} from './RecorderData';
import { DefaultRecordingStatus } from './RecorderTypes';
import { monoFont } from '../components/Panel';

const formatTime = (totalSeconds: number) => {
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const secs = totalSeconds % 60;
  return [hours, minutes, secs].map((v) => (v < 10 ? `0${v}` : v)).join(':');
};

const checkStatus = () => {
  queryRecordingStatus()
    .then((result) => {
      const status = { ...DefaultRecordingStatus, ...result };
      setRecordingStatus(status);
      setIsRecording(status.recording);
      return result;
    })
    .catch(() => {
      /* ignore */
    });
};

const RecordingStatus: React.FC = () => {
  const [recordingStatus] = useRecordingStatus();
  const [{ recordingDuration }] = useRecordingProps();

  useEffect(() => {
    const interval = setInterval(checkStatus, 1000);
    return () => clearInterval(interval);
  }, []);

  if (!recordingStatus.recording) {
    return (
      <Stack
        direction="row"
        alignItems="center"
        spacing={1}
        sx={{
          px: 1.5,
          py: 0.75,
          borderRadius: 999,
          border: 1,
          borderColor: 'divider',
          bgcolor: 'action.hover',
        }}
      >
        <Box
          sx={{
            width: 10,
            height: 10,
            borderRadius: '50%',
            border: 2,
            borderColor: 'text.secondary',
          }}
        />
        <Typography
          sx={{ fontSize: 12, fontWeight: 700, letterSpacing: '0.08em' }}
        >
          NOT RECORDING
        </Typography>
      </Stack>
    );
  }

  const { filename, lastTsMilli, sliceEndMilli } =
    recordingStatus.frameProcessor;
  // Clamped: the slice end can lag the first frame of a new file by a frame.
  const sliceSecs = Math.min(
    recordingDuration,
    Math.max(
      0,
      recordingDuration - Math.ceil((sliceEndMilli - lastTsMilli) / 1000),
    ),
  );

  return (
    <Stack direction="row" alignItems="center" spacing={2}>
      <Stack
        direction="row"
        alignItems="center"
        spacing={1}
        sx={{
          px: 1.5,
          py: 0.75,
          borderRadius: 999,
          border: 1,
          borderColor: (theme) => alpha(theme.palette.error.main, 0.5),
          bgcolor: (theme) => alpha(theme.palette.error.main, 0.12),
        }}
      >
        <Box
          sx={{
            width: 10,
            height: 10,
            borderRadius: '50%',
            bgcolor: 'error.main',
          }}
        />
        <Typography
          color="error"
          sx={{ fontSize: 12, fontWeight: 700, letterSpacing: '0.08em' }}
        >
          REC
        </Typography>
        <Typography sx={{ fontFamily: monoFont, fontSize: 18 }}>
          {formatTime(recordingStatus.recordingDuration)}
        </Typography>
      </Stack>
      {filename && (
        <Stack spacing={0.75} sx={{ minWidth: 200 }}>
          <Typography sx={{ fontFamily: monoFont, fontSize: 13 }}>
            {`${filename}.mp4`}
          </Typography>
          <Stack direction="row" alignItems="center" spacing={1}>
            <LinearProgress
              variant="determinate"
              value={(100 * sliceSecs) / recordingDuration}
              sx={{ flex: 1, height: 4, borderRadius: 2 }}
            />
            <Typography variant="caption" color="text.secondary">
              {`${sliceSecs} / ${recordingDuration} s slice`}
            </Typography>
          </Stack>
        </Stack>
      )}
    </Stack>
  );
};

export default RecordingStatus;
