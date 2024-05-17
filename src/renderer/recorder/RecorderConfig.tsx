import React from 'react';
import { Button, TextField, InputAdornment, Typography } from '@mui/material';
import RecordIcon from '@mui/icons-material/FiberManualRecord';
import { startRecording, stopRecording } from './RecorderApi';
import {
  useRecordingStatus,
  useIsRecording,
  useRecordingStartTime,
  useRecordingProps,
} from './RecorderData';
import { FullSizeWindow } from '../components/FullSizeWindow';
import RGBAImageCanvas from '../components/RGBAImageCanvas';

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
  const [isRecording] = useIsRecording();
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
    (isRecording ? stopRecording() : startRecording()).catch((err) =>
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
    <div
      style={{
        padding: '20px',
        display: 'flex',
        flexDirection: 'column',
        height: '100%',
      }}
    >
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
        size="small"
        label="Recording Filename Prefix"
        variant="outlined"
        fullWidth
        margin="normal"
        value={recordingProps.recordingPrefix}
        onChange={handlePrefixChange}
      />
      <TextField
        size="small"
        label="Recording File Size (seconds)"
        variant="outlined"
        fullWidth
        margin="normal"
        value={String(recordingProps.recordingDuration)}
        onChange={handleIntervalChange}
        type="number"
      />
      {/* <Button
        variant="contained"
        color="primary"
        startIcon={
          isRecording ? <RecordIcon style={{ color: '#ff0000' }} /> : null
        }
        onClick={toggleRecording}
        sx={{ marginTop: '20px' }}
      >
        {isRecording ? 'Stop Recording' : 'Start Recording'}
      </Button> */}
      <div
        style={{
          marginTop: '20px',
          flexGrow: 1,
        }}
      >
        <FullSizeWindow component={RGBAImageCanvas} />
      </div>
    </div>
  );
};

export default RecorderConfig;
