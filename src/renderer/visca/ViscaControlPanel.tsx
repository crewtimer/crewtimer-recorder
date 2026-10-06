import React, { useEffect } from 'react';
import {
  Alert,
  Box,
  Button,
  MenuItem,
  Paper,
  Select,
  SelectChangeEvent,
  Stack,
  FormControlLabel,
  Checkbox,
  Tooltip,
  Typography,
} from '@mui/material';
import OpenInNewIcon from '@mui/icons-material/OpenInNew';
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
  useCameraState,
  useViscaIP,
  useViscaPort,
  useViscaState,
} from './ViscaState';
import ViscaValueButton from './ViscaValueButton';
import RangeStepper from './RangeStepper';
import ViscaPresets from './ViscaPresets';
import { useFocusArea } from '../recorder/RecorderData';
import RotationSelector from '../recorder/RotationSelector';

/** A labelled cluster of controls in the camera control toolbar. */
const Group: React.FC<{
  label: string;
  disabled?: boolean;
  children: React.ReactNode;
}> = ({ label, disabled, children }) => (
  <Box
    role="group"
    aria-label={label}
    sx={{
      display: 'flex',
      flexDirection: 'column',
      gap: 0.75,
      ...(disabled && { opacity: 0.45, pointerEvents: 'none' }),
    }}
  >
    <Typography
      variant="caption"
      color="text.secondary"
      sx={{
        fontWeight: 600,
        letterSpacing: '0.06em',
        textTransform: 'uppercase',
      }}
    >
      {label}
    </Typography>
    <Box
      sx={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: 1 }}
    >
      {children}
    </Box>
  </Box>
);

const ViscaControlPanel: React.FC = () => {
  const [cameraState, setCameraState] = useCameraState();
  const [viscaState] = useViscaState();
  const [focusAreaProps, setFocusAreaProps] = useFocusArea();
  const [viscaIP] = useViscaIP();
  const [viscaPort] = useViscaPort();
  useEffect(() => {
    if (!viscaIP || viscaPort === 0) {
      return;
    }

    const monitor = () => {
      sendViscaCommand({ type: 'AUTO_FOCUS_VALUE' })
        .then(() => {
          return true;
        })
        .catch(() => {});
    };
    monitor();
  }, [viscaIP, viscaPort]);

  // Query camera state when connected
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
    if (viscaState === 'Connected') {
      fetchCameraState();
    }
  }, [setCameraState, viscaState]);

  const onExposureModeChange = async (
    event: SelectChangeEvent<ExposureMode>,
  ) => {
    const exposureMode = event.target.value as ExposureMode;
    setCameraState((prev) => ({ ...prev, exposureMode }));

    switch (exposureMode) {
      case ExposureMode.EXPOSURE_MANUAL:
        await updateCameraState({
          exposureMode,
          iris: cameraState.iris,
          shutter: cameraState.shutter,
          gain: cameraState.gain,
        });
        break;
      case ExposureMode.EXPOSURE_SHUTTER:
        await updateCameraState({ exposureMode, shutter: cameraState.shutter });
        break;
      case ExposureMode.EXPOSURE_IRIS:
        await updateCameraState({ exposureMode, iris: cameraState.iris });
        break;
      case ExposureMode.EXPOSURE_BRIGHT:
        await updateCameraState({
          exposureMode,
          brightness: cameraState.brightness,
        });
        break;
      default:
        await updateCameraState({ exposureMode });
        break;
    }
  };

  const viscaEnabled = viscaPort !== 0;
  const disconnected = viscaEnabled && viscaState === 'Disconnected';

  return (
    <Paper
      variant="outlined"
      sx={{ p: 2, display: 'flex', flexDirection: 'column', gap: 1.5 }}
    >
      {disconnected && (
        <Alert severity="warning">
          {`Camera control (VISCA) is disconnected from ${viscaIP}:${viscaPort}. Check the camera and the VISCA port on the Recorder tab.`}
        </Alert>
      )}
      {!viscaEnabled && (
        <Typography variant="body2" color="text.secondary">
          Camera control is off. Set a VISCA port on the Recorder tab to control
          focus, zoom and exposure.
        </Typography>
      )}
      <Box
        sx={{
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'flex-start',
          gap: 2,
        }}
      >
        {viscaEnabled && (
          <>
            <Group label="Focus" disabled={disconnected}>
              <ViscaValueButton
                name="Focus"
                decrement={{ type: 'FOCUS_OUT' }}
                increment={{ type: 'FOCUS_IN' }}
                reset={{ type: 'FOCUS_RESET' }}
                value={cameraState.autoFocus}
                autoOn={{ type: 'AUTO_FOCUS', value: true }}
                autoOff={{ type: 'AUTO_FOCUS', value: false }}
                autoOnce={{ type: 'FOCUS_ONCE' }}
              />
            </Group>
            <Group label="Zoom" disabled={disconnected}>
              <ViscaValueButton
                name="Zoom"
                decrement={{ type: 'ZOOM_OUT' }}
                increment={{ type: 'ZOOM_IN' }}
                reset={{ type: 'ZOOM_RESET' }}
              />
            </Group>
            <Group label="Exposure" disabled={disconnected}>
              <Stack spacing={1}>
                <Select
                  size="small"
                  value={cameraState.exposureMode}
                  onChange={onExposureModeChange}
                  inputProps={{ 'aria-label': 'Exposure mode' }}
                  sx={{ height: 36, minWidth: 180 }}
                >
                  <MenuItem value={ExposureMode.EXPOSURE_AUTO}>
                    Full auto
                  </MenuItem>
                  <MenuItem value={ExposureMode.EXPOSURE_MANUAL}>
                    Manual
                  </MenuItem>
                  <MenuItem value={ExposureMode.EXPOSURE_SHUTTER}>
                    Shutter priority
                  </MenuItem>
                  <MenuItem value={ExposureMode.EXPOSURE_IRIS}>
                    Iris priority
                  </MenuItem>
                  <MenuItem value={ExposureMode.EXPOSURE_BRIGHT}>
                    Brightness priority
                  </MenuItem>
                </Select>
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
                  cameraState.exposureMode ===
                    ExposureMode.EXPOSURE_SHUTTER) && (
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
                      setCameraState((prev) => ({
                        ...prev,
                        brightness: value,
                      }));
                      sendViscaCommand({ type: 'SET_BRIGHTNESS', value });
                    }}
                  />
                )}
              </Stack>
            </Group>
          </>
        )}
        <Group label="Preview">
          <RotationSelector />
          <Tooltip title="Show a sharpness metric on the preview to help with manual focus">
            <FormControlLabel
              control={
                <Checkbox
                  checked={focusAreaProps.enabled}
                  onChange={() =>
                    setFocusAreaProps((prior) => ({
                      ...prior,
                      enabled: !prior.enabled,
                    }))
                  }
                  size="small"
                />
              }
              label="Focus assist"
              sx={{ m: 0 }}
            />
          </Tooltip>
        </Group>
        {viscaEnabled && (
          <Group label="Presets" disabled={disconnected}>
            <ViscaPresets />
            <Button
              size="small"
              endIcon={<OpenInNewIcon fontSize="small" />}
              onClick={() => window.open(`http://${viscaIP}`)}
            >
              Camera web page
            </Button>
          </Group>
        )}
      </Box>
    </Paper>
  );
};

export default ViscaControlPanel;
