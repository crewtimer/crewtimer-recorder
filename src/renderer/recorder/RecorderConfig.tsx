import React, { useEffect } from 'react';
import {
  TextField,
  Typography,
  Grid,
  MenuItem,
  Checkbox,
  FormControlLabel,
  Tooltip,
  IconButton,
  Button,
} from '@mui/material';
import { UseDatum } from 'react-usedatum';
import FolderOpenIcon from '@mui/icons-material/FolderOpen';
import OpenInNewIcon from '@mui/icons-material/OpenInNew';
import PlayArrowIcon from '@mui/icons-material/PlayArrow';
import StopIcon from '@mui/icons-material/Stop';
import { queryCameraList, startRecording, stopRecording } from './RecorderApi';
import {
  useRecordingStatus,
  useIsRecording,
  useRecordingProps,
} from './RecorderData';
import { FullSizeWindow } from '../components/FullSizeWindow';
import RGBAImageCanvas from '../components/RGBAImageCanvas';
import { showErrorDialog } from '../components/ErrorDialog';
import InfoPopup from '../components/InfoPopup';
import RecorderTips from './RecorderTips';

const { openDirDialog, openFileExplorer } = window.Util;

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
          setCameraList(result?.cameras || []);
          return null;
        })
        .catch(showErrorDialog);
    }, 5000);
    return () => clearInterval(timer);
  }, [isRecording]);

  const chooseDir = () => {
    openDirDialog('Choose Video Folder', recordingProps.recordingFolder)
      .then((result) => {
        if (!result.cancelled) {
          setRecordingProps({
            ...recordingProps,
            recordingFolder: result.path,
          });
        }
        return null;
      })
      .catch(showErrorDialog);
  };

  const handleChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    let value =
      event.target.type === 'checkbox'
        ? event.target.checked
        : event.target.value;

    if (value === 'First Camera Discovered') {
      value = '';
    }

    setRecordingProps({
      ...recordingProps,
      [event.target.name]: value,
    });
  };

  const cameraSelectItems = [
    { name: 'First Camera Discovered', address: '1st' },
    ...cameraList,
  ];
  if (
    recordingProps.networkCamera &&
    !cameraSelectItems.find((c) => c.name === recordingProps.networkCamera)
  ) {
    cameraSelectItems.push({
      name: recordingProps.networkCamera,
      address: '',
    });
  }

  const handleToggleRecording = () => {
    if (isRecording) {
      stopRecording().catch(showErrorDialog);
    } else {
      startRecording().catch(showErrorDialog);
    }
  };

  return (
    <div
      style={{
        padding: '0px 10px',
        display: 'flex',
        flexDirection: 'column',
        height: '100%',
      }}
    >
      <RecordingError />
      <Grid container spacing={2}>
        <Grid item xs={10}>
          <TextField
            select
            margin="normal"
            label="Camera"
            name="networkCamera"
            size="small"
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
        <Grid item xs={2} container justifyContent="center" alignItems="center">
          <Tooltip title="Explore Recording Folder">
            <Button
              variant="contained"
              onClick={handleToggleRecording}
              startIcon={isRecording ? <StopIcon /> : <PlayArrowIcon />}
              sx={{
                backgroundColor: isRecording ? 'red' : 'green',
                '&:hover': {
                  backgroundColor: isRecording ? 'darkred' : 'darkgreen',
                },
              }}
            >
              {isRecording ? 'Stop' : 'Start'}
            </Button>
          </Tooltip>
        </Grid>
        <Grid item xs={10}>
          <TextField
            label="Recording Folder"
            variant="outlined"
            size="small"
            fullWidth
            name="recordingFolder"
            value={recordingProps.recordingFolder}
            onChange={handleChange}
            onClick={chooseDir}
          />
        </Grid>
        <Grid item xs={1}>
          <Tooltip title="Select Recording Folder">
            <IconButton
              color="inherit"
              aria-label="Open Folder"
              onClick={chooseDir}
              size="medium"
            >
              <FolderOpenIcon />
            </IconButton>
          </Tooltip>
        </Grid>
        <Grid item xs={1}>
          <Tooltip title="Explore Recording Folder">
            <IconButton
              color="inherit"
              aria-label="Open Folder"
              onClick={() => openFileExplorer(recordingProps.recordingFolder)}
              size="medium"
            >
              <OpenInNewIcon />
            </IconButton>
          </Tooltip>
        </Grid>
        <Grid item xs={4}>
          <Tooltip title="A prefix added to each recording file.">
            <TextField
              size="small"
              label="Filename Prefix"
              variant="outlined"
              fullWidth
              margin="normal"
              name="recordingPrefix"
              value={recordingProps.recordingPrefix}
              onChange={handleChange}
            />
          </Tooltip>
        </Grid>
        <Grid item xs={4}>
          <Tooltip title="The default duration of each recording file.">
            <TextField
              size="small"
              label="Recording Slice(sec)"
              variant="outlined"
              fullWidth
              margin="normal"
              name="recordingDuration"
              value={String(recordingProps.recordingDuration)}
              onChange={handleChange}
              type="number"
            />
          </Tooltip>
        </Grid>
        <Grid item xs={2}>
          <Tooltip title="If checked, finish line location will be shown on video preview.">
            <FormControlLabel
              control={
                <Checkbox
                  name="showFinishGuide"
                  checked={!!recordingProps.showFinishGuide}
                  onChange={handleChange}
                />
              }
              label="Finish Line"
              sx={{ paddingTop: '1em' }}
            />
          </Tooltip>
        </Grid>
        <Grid item xs={2} container justifyContent="center" alignItems="center">
          <InfoPopup body={<RecorderTips />} />
        </Grid>
      </Grid>
      <div
        style={{
          marginTop: '10px',
          flexGrow: 1,
        }}
      >
        <FullSizeWindow component={RGBAImageCanvas} />
      </div>
    </div>
  );
};

export default RecorderConfig;
