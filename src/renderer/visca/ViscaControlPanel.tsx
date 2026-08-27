import React, { useEffect } from 'react';
import {
  Box,
  Grid,
  MenuItem,
  FormControl,
  Select,
  SelectChangeEvent,
  FormControlLabel,
  Checkbox,
  Tooltip,
  IconButton,
} from '@mui/material';
import CameraIcon from '@mui/icons-material/Camera';
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

const ViscaControlPanel = () => {
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

  // If the VISCA port is not set, do not render the control panel
  if (viscaPort === 0) {
    return null;
  }

  return (
    <Box sx={{ paddingBottom: 1, position: 'relative' }}>
      <Grid container spacing={2}>
        {/* Focus Controls */}
        <Grid
          item
          xs="auto"
          container
          wrap="nowrap"
          alignItems="flex-start"
          justifyContent="flex-start"
          sx={{ flexShrink: 0 }}
        >
          <Box
            component="fieldset"
            sx={{
              margin: 0,
              padding: '2px 8px 7px',
              border: '1px solid',
              borderColor: 'divider',
              borderRadius: 1,
            }}
          >
            <Box
              component="legend"
              sx={{ padding: '0 4px', fontSize: '0.875rem' }}
            >
              Focus
            </Box>
            <ViscaValueButton
              title=""
              decrement={{ type: 'FOCUS_OUT' }}
              increment={{ type: 'FOCUS_IN' }}
              reset={{ type: 'FOCUS_RESET' }}
              value={cameraState.autoFocus}
              autoOn={{ type: 'AUTO_FOCUS', value: true }}
              autoOff={{ type: 'AUTO_FOCUS', value: false }}
              autoOnce={{ type: 'FOCUS_ONCE' }}
              stepButtonsAfterMode
            />
            <Tooltip title="Show Focus metric to assist with manual focus">
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
                    color="primary"
                    size="small"
                  />
                }
                label="Focus Assist"
                sx={{
                  display: 'flex',
                  width: 'fit-content',
                  marginLeft: 0,
                  marginTop: '4px',
                  '& .MuiFormControlLabel-label': { fontSize: '0.95rem' },
                }}
              />
            </Tooltip>
          </Box>
        </Grid>
        <Grid
          item
          xs="auto"
          container
          alignItems="flex-start"
          justifyContent="flex-start"
          sx={{ flexShrink: 0 }}
        >
          <Box
            component="fieldset"
            sx={{
              margin: 0,
              padding: '2px 8px 7px',
              border: '1px solid',
              borderColor: 'divider',
              borderRadius: 1,
            }}
          >
            <Box
              component="legend"
              sx={{ padding: '0 4px', fontSize: '0.875rem' }}
            >
              Zoom
            </Box>
            <ViscaValueButton
              title=""
              decrement={{ type: 'ZOOM_OUT' }}
              increment={{ type: 'ZOOM_IN' }}
              reset={{ type: 'ZOOM_RESET' }}
            />
          </Box>
        </Grid>
        {/* Exposure Controls */}
        <Grid item xs="auto" sx={{ flexShrink: 0 }}>
          <Box
            component="fieldset"
            sx={{
              margin: 0,
              padding: '2px 8px 7px',
              border: '1px solid',
              borderColor: 'divider',
              borderRadius: 1,
            }}
          >
            <Box
              component="legend"
              sx={{ padding: '0 4px', fontSize: '0.875rem' }}
            >
              Exposure
            </Box>
            <Box display="flex" alignItems="flex-start" gap={1}>
              <FormControl
                margin="dense"
                size="small"
                sx={{ minWidth: 120, zIndex: 101 }}
              >
                <Select
                  id="select-id"
                  value={cameraState.exposureMode}
                  onChange={onExposureModeChange}
                  sx={{
                    '.MuiSelect-select': {
                      padding: '6px 14px',
                      fontSize: '0.8rem',
                    },
                  }}
                >
                  <MenuItem value={ExposureMode.EXPOSURE_AUTO}>
                    Full Auto
                  </MenuItem>
                  <MenuItem value={ExposureMode.EXPOSURE_MANUAL}>
                    Manual
                  </MenuItem>
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
              <Box>
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
              </Box>
            </Box>
          </Box>
        </Grid>
        <Grid item xs="auto" sx={{ flexShrink: 0 }}>
          <RotationSelector />
        </Grid>
        <Grid
          item
          xs="auto"
          container
          alignItems="flex-start"
          justifyContent="flex-start"
          sx={{ flexShrink: 0 }}
        >
          <Box
            component="fieldset"
            sx={{
              margin: 0,
              padding: '2px 8px 7px',
              border: '1px solid',
              borderColor: 'divider',
              borderRadius: 1,
            }}
          >
            <Box
              component="legend"
              sx={{ padding: '0 4px', fontSize: '0.875rem' }}
            >
              Control
            </Box>
            <Box
              display="flex"
              flexDirection="column"
              alignItems="flex-start"
              gap={1}
            >
              <ViscaPresets />
              <Tooltip title={`Open Camera Web Page at ${viscaIP}`}>
                <span>
                  <IconButton
                    disabled={viscaState !== 'Connected'}
                    color="inherit"
                    aria-label="Open Camera"
                    onClick={() => window.open(`http://${viscaIP}`)}
                    size="medium"
                  >
                    <CameraIcon />
                  </IconButton>
                </span>
              </Tooltip>
            </Box>
          </Box>
        </Grid>
      </Grid>
      {/* Conditionally render "Disconnected" overlay if explicitly disconnected */}
      {viscaState === 'Disconnected' && (
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
