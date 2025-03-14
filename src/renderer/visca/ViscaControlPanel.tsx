import React, { useEffect } from 'react';
import {
  Box,
  Grid,
  MenuItem,
  FormControl,
  InputLabel,
  Select,
  SelectChangeEvent,
} from '@mui/material';
import {
  sendViscaCommand,
  getCameraState,
  shutterLabels,
  irisLabels,
  updateCameraState,
} from './ViscaAPI';
import { setToast } from '../components/Toast';
import {
  ExposureMode,
  getCameraPresets,
  setCameraPresets,
  useCameraState,
  useViscaState,
} from './ViscaState';
import ViscaValueButton from './ViscaValueButton';
import RangeStepper from './RangeStepper';
import ViscaPresets from './ViscaPresets';

const ViscaControlPanel = () => {
  const [cameraState, setCameraState] = useCameraState();
  const [viscaState] = useViscaState();

  // Query camera state when connected
  useEffect(() => {
    const fetchCameraState = async () => {
      try {
        const result = await getCameraState();
        if (result) {
          // console.log(JSON.stringify(result));
          setCameraState(result);
        }
      } catch (error) {
        setToast({
          severity: 'error',
          msg: `Error querying camera state: ${error}`,
        });
        console.error('Error querying camera state:', error);
      }
    };
    if (viscaState === 'Connected') {
      fetchCameraState();
    }
  }, [setCameraState, viscaState]);

  const onExposureModeChange = async (
    event: SelectChangeEvent<ExposureMode>,
  ) => {
    const presets = [...getCameraPresets()];
    presets[5] = cameraState;
    setCameraPresets(presets);
    const exposureMode = event.target.value as ExposureMode;
    setCameraState((prev) => ({ ...prev, exposureMode }));
    await sendViscaCommand({ type: 'EXPOSURE_MODE', value: exposureMode });
    await updateCameraState({ ...cameraState, exposureMode }); // Apply memorized values after mode changes
  };

  return (
    <Box sx={{ paddingBottom: 1, position: 'relative' }}>
      <Grid container spacing={2}>
        {/* Focus Controls */}
        <Grid
          item
          xs={12}
          md={2}
          container
          alignItems="flex-start"
          justifyContent="flex-start"
          sx={{ marginTop: '8px' }}
        >
          <ViscaValueButton
            title="Focus"
            decrement={{ type: 'FOCUS_OUT' }}
            increment={{ type: 'FOCUS_IN' }}
            reset={{ type: 'FOCUS_RESET' }}
            value={cameraState.autoFocus}
            autoOn={{ type: 'AUTO_FOCUS', value: true }}
            autoOff={{ type: 'AUTO_FOCUS', value: false }}
            autoOnce={{ type: 'FOCUS_ONCE' }}
          />
        </Grid>

        <Grid
          item
          xs={12}
          md={2}
          container
          alignItems="flex-start"
          justifyContent="flex-start"
          sx={{ marginTop: '8px' }}
        >
          <ViscaValueButton
            title="Zoom"
            decrement={{ type: 'ZOOM_OUT' }}
            increment={{ type: 'ZOOM_IN' }}
            reset={{ type: 'ZOOM_RESET' }}
          />
        </Grid>

        {/* Exposure Controls */}
        <Grid item xs={12} md={2}>
          <FormControl
            margin="dense"
            size="small" // makes form components (including Select) smaller
            sx={{ minWidth: 120, zIndex: 101 }}
          >
            <InputLabel id="select-label">Exposure Mode</InputLabel>
            <Select
              id="select-id"
              labelId="select-label"
              label="Exposure Mode"
              value={cameraState.exposureMode}
              onChange={onExposureModeChange}
              // Option A: put smaller padding via sx
              sx={{
                // The select container; tweak as you like
                // smaller vertical & horizontal padding
                '.MuiSelect-select': {
                  padding: '6px 14px',
                  fontSize: '0.8rem',
                },
              }}
              // Option B: if you prefer an inline style for the displayed value
              // SelectDisplayProps={{ style: { padding: '6px 14px' } }}
            >
              <MenuItem value={ExposureMode.EXPOSURE_AUTO}>Full Auto</MenuItem>
              <MenuItem value={ExposureMode.EXPOSURE_MANUAL}>Manual</MenuItem>
              <MenuItem value={ExposureMode.EXPOSURE_SHUTTER}>
                Shutter Priority
              </MenuItem>
              <MenuItem value={ExposureMode.EXPOSURE_IRIS}>
                Iris Priority
              </MenuItem>
              <MenuItem value={ExposureMode.EXPOSURE_BRIGHT}>
                Brightness Priority
              </MenuItem>
            </Select>
          </FormControl>
        </Grid>
        <Grid item xs={12} md={3}>
          {(cameraState.exposureMode === ExposureMode.EXPOSURE_MANUAL ||
            cameraState.exposureMode === ExposureMode.EXPOSURE_IRIS) && (
            <RangeStepper
              title="Iris"
              min={0}
              max={13}
              step={-1}
              labels={irisLabels}
              value={cameraState.iris}
              onChange={(value) => {
                setCameraState((prev) => ({ ...prev, iris: value }));
                sendViscaCommand({ type: 'SET_IRIS', value });
              }}
            />
          )}
          {(cameraState.exposureMode === ExposureMode.EXPOSURE_MANUAL ||
            cameraState.exposureMode === ExposureMode.EXPOSURE_SHUTTER) && (
            <RangeStepper
              title="Shutter"
              min={5}
              max={21}
              labels={shutterLabels}
              value={cameraState.shutter}
              onChange={(value) => {
                setCameraState((prev) => ({ ...prev, shutter: value }));
                sendViscaCommand({ type: 'SET_SHUTTER', value });
              }}
            />
          )}
          {cameraState.exposureMode === ExposureMode.EXPOSURE_MANUAL && (
            <RangeStepper
              title="Gain"
              min={0}
              max={15}
              value={cameraState.gain}
              onChange={(value) => {
                setCameraState((prev) => ({ ...prev, gain: value }));
                sendViscaCommand({ type: 'SET_GAIN', value });
              }}
            />
          )}
          {cameraState.exposureMode === ExposureMode.EXPOSURE_BRIGHT && (
            <RangeStepper
              title="Bright"
              min={0}
              max={27}
              value={cameraState.brightness}
              onChange={(value) => {
                setCameraState((prev) => ({ ...prev, brightness: value }));
                sendViscaCommand({ type: 'SET_BRIGHTNESS', value });
              }}
            />
          )}
        </Grid>
        <Grid
          item
          xs={12}
          md={2}
          container
          alignItems="flex-start"
          justifyContent="flex-start"
        >
          <ViscaPresets />
        </Grid>
      </Grid>
      {/* Conditionally render "Disconnected" overlay if not connected */}
      {viscaState !== 'Connected' && (
        <Box
          position="absolute"
          top={0}
          left={0}
          width="100%"
          height="100%"
          display="flex"
          justifyContent="center"
          alignItems="center"
          bgcolor="rgba(0, 0, 0, 0.4)"
          color="#fff"
          fontSize="1.5rem"
          zIndex={9999}
        >
          Camera control channel disconnected
        </Box>
      )}
    </Box>
  );
};

export default ViscaControlPanel;
