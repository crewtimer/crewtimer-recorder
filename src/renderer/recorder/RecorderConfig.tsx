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
} from '@mui/material';
import FolderOpenIcon from '@mui/icons-material/FolderOpen';
import OpenInNewIcon from '@mui/icons-material/OpenInNew';
import {
  useRecordingStatus,
  useRecordingProps,
  useRecordingPropsPending,
  useWaypointList,
} from './RecorderData';
import { FullSizeWindow } from '../components/FullSizeWindow';
import RGBAImageCanvas from '../components/RGBAImageCanvas';
import { showErrorDialog } from '../components/ErrorDialog';
import InfoPopup from '../components/InfoPopup';
import RecorderTips from './RecorderTips';
import { useViscaIP } from '../visca/ViscaState';
import { useCameraList } from './CameraMonitor';
import { ViscaPortSelector } from '../visca/ViscaPortSelector';
import { updateSettings } from './RecorderApi';

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

const RecorderConfig: React.FC = () => {
  const [recordingProps, setRecordingProps] = useRecordingProps();
  const [, setRecordingPropsPending] = useRecordingPropsPending();
  const [cameraList] = useCameraList();
  const [, setViscaIP] = useViscaIP();
  const [wpList] = useWaypointList();
  const { waypoint } = recordingProps;
  const waypointList = [...wpList];
  if (waypoint && !waypointList.includes(waypoint)) {
    waypointList.push(waypoint);
  }

  useEffect(() => {
    updateSettings({ waypoint });
  }, [waypoint]);

  const chooseDir = () => {
    openDirDialog('Choose Video Folder', recordingProps.recordingFolder)
      .then((result) => {
        if (!result.cancelled) {
          setRecordingPropsPending(true);
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
    const value =
      event.target.type === 'checkbox'
        ? event.target.checked
        : event.target.value;

    if (['recordingDuration', 'recordingPrefix'].includes(event.target.name)) {
      setRecordingPropsPending(true);
    }
    setRecordingProps({
      ...recordingProps,
      [event.target.name]: value,
    });
  };

  const handleCameraChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    const networkIP =
      cameraList
        .find((c) => c.name === event.target.value)
        ?.address.replace(/:.*/, '') || '';
    setRecordingPropsPending(true);
    setRecordingProps({
      ...recordingProps,
      networkCamera: event.target.value,
      networkIP,
    });
    // update the Camera IP

    setViscaIP(networkIP || '');
  };

  const selectedCamera = recordingProps.networkCamera;
  const camFound = cameraList.some((c) => c.name === selectedCamera);

  // console.log(
  //   JSON.stringify({ cameraList, camFound, selectedCamera, viscaIP }, null, 2),
  // );

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
        <Grid item xs={8}>
          <TextField
            select
            margin="normal"
            label="Camera"
            name="networkCamera"
            size="small"
            value={selectedCamera}
            onChange={handleCameraChange}
            fullWidth
            // Color the selected text red if invalid
            sx={{
              '& .MuiSelect-select': {
                color: camFound ? 'inherit' : 'red',
              },
            }}
          >
            {cameraList.map((camera) => (
              <MenuItem key={camera.name} value={camera.name}>
                {camera.name}
              </MenuItem>
            ))}
            {/* If the current value is not in the valid list, show a red fallback option */}
            {!camFound && selectedCamera && (
              <MenuItem value={selectedCamera} style={{ color: 'red' }}>
                {selectedCamera}
              </MenuItem>
            )}
          </TextField>
        </Grid>
        <Grid item xs={2} container alignItems="center">
          <ViscaPortSelector />
        </Grid>
        <Grid item xs={2} container alignItems="center" />
        <Grid item xs={8}>
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
        <Grid item xs={2} />
        <Grid item xs={2}>
          <Tooltip
            placement="top"
            title="A prefix added to each recording file."
          >
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
        <Grid item xs={2}>
          <Tooltip
            placement="top"
            title="The default duration of each recording file."
          >
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
        <Grid item xs={4}>
          <Tooltip
            placement="top"
            title="Bind this recorder instance to the selected Video Review waypoint"
          >
            <TextField
              select
              size="small"
              label="Waypoint"
              variant="outlined"
              fullWidth
              margin="normal"
              value={waypoint || 'Any'}
              onChange={(e) =>
                setRecordingProps({
                  ...recordingProps,
                  waypoint: e.target.value === 'Any' ? '' : e.target.value,
                })
              }
            >
              <MenuItem value="Any">Any</MenuItem>
              {waypointList.map((wp) => (
                <MenuItem key={wp} value={wp}>
                  {wp}
                </MenuItem>
              ))}
            </TextField>
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
