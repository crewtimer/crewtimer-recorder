import React, { useEffect, useRef, useState } from 'react';
import {
  Alert,
  Box,
  Button,
  MenuItem,
  Paper,
  Select,
  SelectChangeEvent,
  Slider,
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
  getLensPosition,
  shutterLabels,
  irisLabels,
  updateCameraState,
} from './ViscaAPI';
import { setToast } from '../components/Toast';
import {
  ExposureMode,
  FocusReach,
  getFocusReach,
  getLensRange,
  setFocusReach,
  setLensRange,
  useCameraState,
  useFocusReach,
  useLensRange,
  useViscaIP,
  useViscaPort,
  useViscaState,
  zoomBand,
} from './ViscaState';
import ViscaValueButton from './ViscaValueButton';
import RangeStepper from './RangeStepper';
import ViscaPresets from './ViscaPresets';
import ViscaQualification from './ViscaQualification';
import { useFocusArea } from '../recorder/RecorderData';
import RotationSelector from '../recorder/RotationSelector';

/** A labelled cluster of controls in the camera control toolbar. */
const Group: React.FC<{
  label: string;
  disabled?: boolean;
  children: React.ReactNode;
}> = ({ label, disabled, children }) => (
  <Paper
    variant="outlined"
    role="group"
    aria-label={label}
    sx={{
      p: 1.5,
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
  </Paper>
);

/**
 * The lens position, and when a range is qualified, a slider to send the lens straight to a
 * position. A direct position move runs at the camera's top speed. The mark shows where the
 * lens was sent until it gets there, or until it stops short at the lens's limit. Ends known
 * to be out of reach are grayed out; a hard end can't be dragged past.
 */
const LensSlider: React.FC<{
  label: string;
  value: number;
  range?: { min: number; max: number };
  reach?: { low?: number; high?: number; lowHard: boolean; highHard: boolean };
  onSet: (value: number) => void;
  onLimit?: (value: number, end: 'low' | 'high') => void;
}> = ({ label, value, range, reach, onSet, onLimit }) => {
  const [drag, setDrag] = useState<number>();
  const [target, setTarget] = useState<number>();
  const [limit, setLimit] = useState<number>();
  const tolerance = range ? (range.max - range.min) * 0.005 : 0;
  // The parent passes a new callback every render; the limit timer must not restart on it.
  const onLimitRef = useRef(onLimit);
  onLimitRef.current = onLimit;

  useEffect(() => {
    if (target === undefined) return undefined;
    if (Math.abs(value - target) <= tolerance) {
      setTarget(undefined);
      return undefined;
    }
    // Every position change restarts this; a lens still for 1.5 s short of the target is at
    // its limit (focus travel narrows with zoom).
    const timer = setTimeout(() => {
      onLimitRef.current?.(value, target > value ? 'high' : 'low');
      setLimit(value);
      setTarget(undefined);
    }, 1500);
    return () => clearTimeout(timer);
  }, [target, tolerance, value]);

  useEffect(() => {
    if (limit !== undefined && value !== limit) setLimit(undefined);
  }, [limit, value]);

  let status = `${value}`;
  if (target !== undefined) status = `${value} → ${target}`;
  else if (limit !== undefined) status = `${value} – lens limit`;

  const low = Math.max(range?.min ?? 0, reach?.low ?? -Infinity);
  const high = Math.min(range?.max ?? 0, reach?.high ?? Infinity);
  const clamp = (v: number) =>
    Math.min(
      reach?.highHard ? high : Infinity,
      Math.max(reach?.lowHard ? low : -Infinity, v),
    );
  const percent = (v: number) =>
    range ? ((v - range.min) / (range.max - range.min)) * 100 : 0;
  const gray = (left: number, width: number, hard?: boolean) => (
    <Box
      sx={{
        position: 'absolute',
        top: '50%',
        left: `${left}%`,
        width: `${width}%`,
        height: 4,
        transform: 'translateY(-50%)',
        bgcolor: hard ? 'action.disabled' : 'action.disabledBackground',
        zIndex: 1,
        pointerEvents: 'none',
      }}
    />
  );

  return (
    <Box sx={{ width: '100%' }}>
      <Typography
        variant="body2"
        color={limit === undefined ? undefined : 'warning.main'}
        sx={{ fontVariantNumeric: 'tabular-nums' }}
      >
        {status}
      </Typography>
      {range && (
        <>
          <Box sx={{ position: 'relative' }}>
            {low > range.min && gray(0, percent(low), reach?.lowHard)}
            {high < range.max &&
              gray(percent(high), 100 - percent(high), reach?.highHard)}
            <Slider
              size="small"
              aria-label={label}
              min={range.min}
              max={range.max}
              value={drag ?? Math.min(range.max, Math.max(range.min, value))}
              marks={target === undefined ? false : [{ value: target }]}
              valueLabelDisplay="auto"
              sx={{
                // Taller than the thumb and orange, so the target shows over the filled track too.
                '& .MuiSlider-mark, & .MuiSlider-markActive': {
                  width: 3,
                  height: 22,
                  borderRadius: 1,
                  bgcolor: 'warning.main',
                  opacity: 1,
                },
              }}
              onChange={(_event, v) => setDrag(clamp(v as number))}
              onChangeCommitted={(_event, v) => {
                setDrag(undefined);
                setLimit(undefined);
                setTarget(clamp(v as number));
                onSet(clamp(v as number));
              }}
            />
          </Box>
          <Box
            sx={{
              display: 'flex',
              justifyContent: 'space-between',
              typography: 'caption',
              color: 'text.secondary',
              mt: -1,
            }}
          >
            <span>{range.min}</span>
            <span>{range.max}</span>
          </Box>
        </>
      )}
    </Box>
  );
};

/** Updates what's known about the focus reach in the zoom band of a zoom position. */
const updateReach = (
  zoom: number,
  change: (reach: FocusReach) => FocusReach,
) => {
  const range = getLensRange();
  if (!range) return;
  const band = zoomBand(zoom, range.zoom);
  const reach = getFocusReach();
  const current = reach[band] ?? {};
  const next = change(current);
  if (next === current) return;
  setFocusReach({ ...reach, [band]: next });
};

const ViscaControlPanel: React.FC = () => {
  const [cameraState, setCameraState] = useCameraState();
  const [viscaState] = useViscaState();
  const [focusAreaProps, setFocusAreaProps] = useFocusArea();
  const [viscaIP] = useViscaIP();
  const [viscaPort] = useViscaPort();
  const [lensRange] = useLensRange();
  const [focusReach] = useFocusReach();
  const bandReach =
    lensRange && focusReach[zoomBand(cameraState.zoom, lensRange.zoom)];
  // An end found at (nearly) this zoom is a hard stop; from elsewhere in the band, a hint.
  const atZoom = (zoom?: number) =>
    !!lensRange &&
    zoom !== undefined &&
    Math.abs(cameraState.zoom - zoom) <=
      (lensRange.zoom.max - lensRange.zoom.min) * 0.01;
  const focusReachView = bandReach && {
    low: bandReach.low,
    high: bandReach.high,
    lowHard: atZoom(bandReach.lowZoom),
    highHard: atZoom(bandReach.highZoom),
  };
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

  // Follow zoom and focus as they move, from the buttons, autofocus or a lens sweep.
  useEffect(() => {
    if (viscaState !== 'Connected') {
      return undefined;
    }
    let stopped = false;
    // Each read waits for the last one, so slow replies can't pile up in the VISCA queue.
    const poll = async () => {
      try {
        const zoom = await getLensPosition('zoom');
        const focus = await getLensPosition('focus');
        setCameraState((prev) => ({ ...prev, zoom, focus }));
        // A position past a saved end means the sweep stopped short; widen the range to it.
        const range = getLensRange();
        if (
          range &&
          (zoom < range.zoom.min ||
            zoom > range.zoom.max ||
            focus < range.focus.min ||
            focus > range.focus.max)
        ) {
          setLensRange({
            zoom: {
              min: Math.min(range.zoom.min, zoom),
              max: Math.max(range.zoom.max, zoom),
            },
            focus: {
              min: Math.min(range.focus.min, focus),
              max: Math.max(range.focus.max, focus),
            },
          });
        }
        // Focus seen past a learned limit means that limit came from elsewhere in the band.
        // The widened end is only seen, not a proven stop, so it loses its zoom.
        updateReach(zoom, (reach) => {
          let next = reach;
          if (reach.low !== undefined && focus < reach.low) {
            next = { ...next, low: focus, lowZoom: undefined };
          }
          if (reach.high !== undefined && focus > reach.high) {
            next = { ...next, high: focus, highZoom: undefined };
          }
          return next;
        });
      } catch (error) {
        console.warn(`Lens position poll failed: ${error}`);
      }
      if (!stopped) setTimeout(poll, 500);
    };
    poll();
    return () => {
      stopped = true;
    };
  }, [setCameraState, viscaState]);

  const onExposureModeChange = async (
    event: SelectChangeEvent<ExposureMode>,
  ) => {
    const exposureMode = event.target.value as ExposureMode;
    // Start from what the camera uses right now (e.g. what auto exposure chose),
    // not from the values read when VISCA connected.
    const live = await getCameraState().catch((error) => {
      setToast({
        severity: 'error',
        msg: `Error reading camera exposure: ${error}`,
      });
      return undefined;
    });
    if (!live) return;
    setCameraState({ ...live, exposureMode });

    switch (exposureMode) {
      case ExposureMode.EXPOSURE_MANUAL:
        await updateCameraState({
          exposureMode,
          iris: live.iris,
          shutter: live.shutter,
          gain: live.gain,
        });
        break;
      case ExposureMode.EXPOSURE_SHUTTER:
        await updateCameraState({ exposureMode, shutter: live.shutter });
        break;
      case ExposureMode.EXPOSURE_IRIS:
        await updateCameraState({ exposureMode, iris: live.iris });
        break;
      case ExposureMode.EXPOSURE_BRIGHT:
        await updateCameraState({
          exposureMode,
          brightness: live.brightness,
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
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: 1.5 }}>
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
          alignItems: 'stretch',
          gap: 1,
        }}
      >
        {viscaEnabled && (
          <>
            <Group label="Focus" disabled={disconnected}>
              <ViscaValueButton
                name="Focus"
                decrement={{ type: 'FOCUS_FAR' }}
                increment={{ type: 'FOCUS_NEAR' }}
                reset={{ type: 'FOCUS_RESET' }}
                value={cameraState.autoFocus}
                autoOn={{ type: 'AUTO_FOCUS', value: true }}
                autoOff={{ type: 'AUTO_FOCUS', value: false }}
                autoOnce={{ type: 'FOCUS_ONCE' }}
              />
              <LensSlider
                label="Focus position"
                value={cameraState.focus}
                range={lensRange?.focus}
                reach={focusReachView}
                onLimit={(value, end) =>
                  updateReach(cameraState.zoom, (reach) => ({
                    ...reach,
                    [end]: value,
                    [`${end}Zoom`]: cameraState.zoom,
                  }))
                }
                onSet={async (value) => {
                  // The camera ignores a focus position while autofocus is on.
                  setCameraState((prev) => ({ ...prev, autoFocus: false }));
                  await sendViscaCommand({ type: 'AUTO_FOCUS', value: false });
                  await sendViscaCommand({ type: 'SET_FOCUS', value });
                }}
              />
            </Group>
            <Group label="Zoom" disabled={disconnected}>
              <ViscaValueButton
                name="Zoom"
                decrement={{ type: 'ZOOM_OUT' }}
                increment={{ type: 'ZOOM_IN' }}
                reset={{ type: 'ZOOM_RESET' }}
              />
              <LensSlider
                label="Zoom position"
                value={cameraState.zoom}
                range={lensRange?.zoom}
                onSet={(value) => sendViscaCommand({ type: 'SET_ZOOM', value })}
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
        {viscaEnabled && (
          <Group label="Camera Qualification" disabled={disconnected}>
            <ViscaQualification />
          </Group>
        )}
      </Box>
    </Box>
  );
};

export default ViscaControlPanel;
