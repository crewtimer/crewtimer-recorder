import React, { useEffect } from 'react';
import {
  Box,
  Button,
  Grid,
  Slider,
  Typography,
  MenuItem,
  FormControl,
  InputLabel,
  Select,
  SelectChangeEvent,
} from '@mui/material';
import {
  sendViscaCommand,
  ViscaCommand,
  updateCameraState,
  getCameraState,
  shutterLabels,
  irisLabels,
} from './ViscaAPI';
import { setToast } from '../components/Toast';
import { CameraState, ExposureMode, useCameraState } from './ViscaState';
import ViscaValueButton from './ViscaValueButton';
import ViscaNumberRange from './RangeStepper';
import RangeStepper from './RangeStepper';

const ViscaControlPanel = () => {
  const [cameraState, setCameraState] = useCameraState();

  const handleCommand = async (command: ViscaCommand) => {
    try {
      const result = await sendViscaCommand(command);
      if (result) {
        // const state = await getCameraState();
        // if (state) {
        //   setCameraState(state);
        // }
      }
    } catch (error) {
      setToast({
        severity: 'error',
        msg: `Error sending VISCA command: ${error}`,
      });
      console.error('Error sending VISCA command:', error);
    }
  };

  const handleSetState = async (newState: Partial<CameraState>) => {
    try {
      await updateCameraState(newState);
      setCameraState((prev) => ({ ...prev, ...newState }));
    } catch (error) {
      setToast({
        severity: 'error',
        msg: `Error setting VISCA state: ${error}`,
      });
      console.error('Error setting VISCA state:', error);
    }
  };

  // Query camera state on mount
  useEffect(() => {
    const fetchCameraState = async () => {
      try {
        const result = await getCameraState();
        if (result) {
          console.log(JSON.stringify(result));
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
    fetchCameraState();
  }, [setCameraState]);

  const onExposureModeChange = (event: SelectChangeEvent<ExposureMode>) => {
    const exposureMode = event.target.value as ExposureMode;
    setCameraState((prev) => ({ ...prev, exposureMode }));
    sendViscaCommand({ type: 'EXPOSURE_MODE', value: exposureMode });
  };

  return (
    <Box sx={{ p: 2 }}>
      <Grid container spacing={2}>
        {/* Focus Controls */}
        <Grid item xs={12} md={3}>
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
        <Grid item xs={12} md={3}>
          <ViscaValueButton
            title="Zoom"
            decrement={{ type: 'ZOOM_OUT' }}
            increment={{ type: 'ZOOM_IN' }}
            reset={{ type: 'ZOOM_RESET' }}
          />
        </Grid>

        {/* Exposure Controls */}
        <Grid item xs={12} md={3}>
          <FormControl
            margin="dense"
            size="small" // makes form components (including Select) smaller
            sx={{ minWidth: 120 }}
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
      </Grid>
    </Box>
  );
};

export default ViscaControlPanel;
