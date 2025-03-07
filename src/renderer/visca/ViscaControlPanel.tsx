import React, { useState, useEffect } from 'react';
import {
  Box,
  Button,
  Grid,
  Slider,
  Typography,
  FormControlLabel,
  Switch,
} from '@mui/material';
import { sendViscaCommand, CameraState, ViscaCommand } from './ViscaAPI';
import { setToast } from '../components/Toast';

const ViscaControlPanel = () => {
  const [cameraState, setCameraState] = useState<CameraState>({
    autoFocus: false,
    autoExposure: false,
    iris: 0,
    shutter: 0,
    gain: 0,
  });

  const handleCommand = async (command: ViscaCommand) => {
    try {
      const result = await sendViscaCommand({ data: command });
      if (result) {
        setCameraState(result);
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
      await sendViscaCommand({
        data: { type: 'SET_ALL_STATES', value: newState },
      });
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
        const result = await sendViscaCommand({
          data: { type: 'QUERY_ALL_STATES' },
        });
        if (result) {
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
  }, []);

  return (
    <Box sx={{ p: 2 }}>
      <Typography variant="h6" gutterBottom>
        VISCA Camera Controls
      </Typography>

      <Grid container spacing={2}>
        {/* Focus Controls */}
        <Grid item xs={12} md={4}>
          <Typography variant="subtitle1">Focus</Typography>
          <FormControlLabel
            control={
              <Switch
                checked={cameraState.autoFocus}
                onChange={(event) =>
                  handleSetState({ autoFocus: event.target.checked })
                }
              />
            }
            label="Auto Focus"
          />
          <Button
            variant="contained"
            onClick={() => handleCommand({ type: 'FOCUS_IN' })}
            disabled={cameraState.autoFocus}
          >
            Focus In
          </Button>
          <Button
            variant="contained"
            onClick={() => handleCommand({ type: 'FOCUS_OUT' })}
            disabled={cameraState.autoFocus}
          >
            Focus Out
          </Button>
        </Grid>

        {/* Zoom Controls */}
        <Grid item xs={12} md={4}>
          <Typography variant="subtitle1">Zoom</Typography>
          <Button
            variant="contained"
            onClick={() => handleCommand({ type: 'ZOOM_IN' })}
          >
            Zoom In
          </Button>
          <Button
            variant="contained"
            onClick={() => handleCommand({ type: 'ZOOM_OUT' })}
          >
            Zoom Out
          </Button>
        </Grid>

        {/* Exposure Controls */}
        <Grid item xs={12} md={4}>
          <Typography variant="subtitle1">Exposure</Typography>
          <FormControlLabel
            control={
              <Switch
                checked={cameraState.autoExposure}
                onChange={(event) =>
                  handleSetState({ autoExposure: event.target.checked })
                }
              />
            }
            label="Auto Exposure"
          />
          {/* Iris Controls */}
          <Box sx={{ mt: 2 }}>
            <Typography id="iris-slider" gutterBottom>
              Iris: {cameraState.iris}
            </Typography>
            <Slider
              aria-labelledby="iris-slider"
              value={cameraState.iris}
              min={0}
              max={15}
              step={1}
              onChange={(event, newValue) =>
                handleSetState({ iris: newValue as number })
              }
              disabled={cameraState.autoExposure}
            />
            <Button
              variant="contained"
              onClick={() => handleCommand({ type: 'IRIS_UP' })}
              disabled={cameraState.autoExposure}
            >
              Iris Up
            </Button>
            <Button
              variant="contained"
              onClick={() => handleCommand({ type: 'IRIS_DOWN' })}
              disabled={cameraState.autoExposure}
            >
              Iris Down
            </Button>
          </Box>
          {/* Shutter Controls */}
          <Box sx={{ mt: 2 }}>
            <Typography id="shutter-slider" gutterBottom>
              Shutter: {cameraState.shutter}
            </Typography>
            <Slider
              aria-labelledby="shutter-slider"
              value={cameraState.shutter}
              min={0}
              max={15}
              step={1}
              onChange={(event, newValue) =>
                handleSetState({ shutter: newValue as number })
              }
              disabled={cameraState.autoExposure}
            />
            <Button
              variant="contained"
              onClick={() => handleCommand({ type: 'SHUTTER_UP' })}
              disabled={cameraState.autoExposure}
            >
              Shutter Up
            </Button>
            <Button
              variant="contained"
              onClick={() => handleCommand({ type: 'SHUTTER_DOWN' })}
              disabled={cameraState.autoExposure}
            >
              Shutter Down
            </Button>
          </Box>
          {/* Gain controls */}
          <Box sx={{ mt: 2 }}>
            <Typography id="gain-slider" gutterBottom>
              Gain: {cameraState.gain}
            </Typography>
            <Slider
              aria-labelledby="gain-slider"
              value={cameraState.gain}
              min={0}
              max={15}
              step={1}
              onChange={(event, newValue) =>
                handleSetState({ gain: newValue as number })
              }
              disabled={cameraState.autoExposure}
            />
            <Button
              variant="contained"
              onClick={() => handleCommand({ type: 'GAIN_UP' })}
              disabled={cameraState.autoExposure}
            >
              Gain Up
            </Button>
            <Button
              variant="contained"
              onClick={() => handleCommand({ type: 'GAIN_DOWN' })}
              disabled={cameraState.autoExposure}
            >
              Gain Down
            </Button>
          </Box>
        </Grid>
      </Grid>
    </Box>
  );
};

export default ViscaControlPanel;
