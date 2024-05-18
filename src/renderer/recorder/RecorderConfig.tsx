import React, { useEffect } from 'react';
import { TextField, Typography, Grid, MenuItem } from '@mui/material';
import { UseDatum } from 'react-usedatum';
import { queryCameraList } from './RecorderApi';
import {
  useRecordingStatus,
  useIsRecording,
  useRecordingProps,
} from './RecorderData';
import { FullSizeWindow } from '../components/FullSizeWindow';
import RGBAImageCanvas from '../components/RGBAImageCanvas';

const { openDirDialog } = window.Util;

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

const [useCameraList, setCameraList] = UseDatum<
  { name: string; address: string }[]
>([]);
const RecorderConfig: React.FC = () => {
  const [isRecording] = useIsRecording();
  const [recordingProps, setRecordingProps] = useRecordingProps();
  const [cameraList] = useCameraList();

  useEffect(() => {
    if (isRecording) {
      return () => {};
    }
    const timer = setInterval(() => {
      queryCameraList()
        .then((result) => {
          setCameraList(result.cameras || []);
          return null;
        })
        .catch((err) => console.error(err));
    }, 5000);
    return () => clearInterval(timer);
  }, [isRecording]);

  const chooseDir = () => {
    openDirDialog('Choose Video Directory', recordingProps.recordingFolder)
      .then((result) => {
        if (!result.cancelled) {
          setRecordingProps({
            ...recordingProps,
            recordingFolder: result.path,
          });
        }
        return null;
      })
      .catch((e) => console.error(e));
  };

  const handleChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    const value =
      event.target.value === 'First Camera Discovered'
        ? ''
        : event.target.value;
    setRecordingProps({
      ...recordingProps,
      [event.target.name]: value,
    });
  };

  const cameraSelectItems = [
    { name: 'First Camera Discovered', address: '1st' },
    ...cameraList,
  ];
  if (!cameraSelectItems.find((c) => c.name === recordingProps.networkCamera)) {
    cameraSelectItems.push({
      name: recordingProps.networkCamera,
      address: '',
    });
  }

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
      <Grid container spacing={2}>
        <Grid item xs={12}>
          <TextField
            select
            margin="normal"
            required
            label="Camera"
            name="networkCamera"
            value={recordingProps.networkCamera || 'First Camera Discovered'}
            onChange={handleChange}
            fullWidth
          >
            {cameraSelectItems.map((camera) => (
              <MenuItem key={camera.name} value={camera.name}>
                {camera.name}
              </MenuItem>
            ))}
          </TextField>
        </Grid>
        <Grid item xs={12}>
          <TextField
            label="Recording Folder"
            variant="outlined"
            fullWidth
            name="recordingFolder"
            value={recordingProps.recordingFolder}
            onChange={handleChange}
            onClick={chooseDir}
          />
        </Grid>
        <Grid item xs={6}>
          <TextField
            size="small"
            label="Recording Filename Prefix"
            variant="outlined"
            fullWidth
            margin="normal"
            name="recordingPrefix"
            value={recordingProps.recordingPrefix}
            onChange={handleChange}
          />
        </Grid>
        <Grid item xs={6}>
          <TextField
            size="small"
            label="Recording File Size (seconds)"
            variant="outlined"
            fullWidth
            margin="normal"
            name="recordingDuration"
            value={String(recordingProps.recordingDuration)}
            onChange={handleChange}
            type="number"
          />
        </Grid>
      </Grid>
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
