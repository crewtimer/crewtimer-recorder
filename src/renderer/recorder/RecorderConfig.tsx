import React from 'react';
import { Button, TextField, InputAdornment, Typography } from '@mui/material';
import RecordIcon from '@mui/icons-material/FiberManualRecord';
import {
  startRecording,
  stopRecording,
  useIsRecording,
  useRecordingProps,
  useRecordingStartTime,
  useRecordingStatus,
} from './RecorderApi';

const RecordingError = () => {
  const [recordingStatus] = useRecordingStatus();
  return recordingStatus.error ? (
    <Typography
      variant="body2"
      color="error"
      sx={{
        marginBottom: '1em',
        border: '1px solid red',
        padding: '8px',
      }}
    >
      {recordingStatus.error}
    </Typography>
  ) : null;
};
const RecorderConfig: React.FC = () => {
  const [isRecording, setIsRecording] = useIsRecording();
  const [, setRecordingStartTime] = useRecordingStartTime();
  const [recordingProps, setRecordingProps] = useRecordingProps();

  const handleFolderChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    setRecordingProps({
      ...recordingProps,
      recordingFolder: event.target.value,
    });
  };

  const handlePrefixChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    setRecordingProps({
      ...recordingProps,
      recordingPrefix: event.target.value,
    });
  };

  const handleIntervalChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    setRecordingProps({
      ...recordingProps,
      recordingDuration: parseInt(event.target.value, 10),
    });
  };

  const toggleRecording = () => {
    setRecordingStartTime(Date.now());
    const newRecordingState = !isRecording;
    setIsRecording(newRecordingState);
    (newRecordingState ? startRecording() : stopRecording()).catch((err) =>
      console.error(err),
    );
  };

  // const chooseDir = () => {
  //   openDirDialog('Choose Video Directory', videoDir)
  //     .then((result) => {
  //       if (!result.cancelled) {
  //         if (result.path !== videoDir) {
  //           setVideoDir(result.path);
  //         }
  //       }
  //     })
  //     .catch();
  // };

  return (
    <div style={{ padding: '20px' }}>
      <RecordingError />
      <TextField
        label="Recording Folder"
        variant="outlined"
        fullWidth
        value={recordingProps.recordingFolder}
        onChange={handleFolderChange}
        InputProps={{
          startAdornment: (
            <InputAdornment position="start">
              <Button variant="contained" component="label">
                Select Folder
                <input type="file" hidden onChange={handleFolderChange} />
              </Button>
            </InputAdornment>
          ),
        }}
      />
      <TextField
        label="Recording Prefix"
        variant="outlined"
        fullWidth
        margin="normal"
        value={recordingProps.recordingPrefix}
        onChange={handlePrefixChange}
      />
      <TextField
        label="Recording Interval (seconds)"
        variant="outlined"
        fullWidth
        margin="normal"
        value={String(recordingProps.recordingDuration)}
        onChange={handleIntervalChange}
        type="number"
      />
      <Button
        variant="contained"
        color="primary"
        startIcon={
          isRecording ? <RecordIcon style={{ color: '#ff0000' }} /> : null
        }
        onClick={toggleRecording}
        sx={{ marginTop: '20px' }}
      >
        {isRecording ? 'Stop Recording' : 'Start Recording'}
      </Button>
    </div>
  );
};

export default RecorderConfig;
