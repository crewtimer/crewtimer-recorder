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
  driveFocus,
  getLensPosition,
  settleLens,
  shutterLabels,
  irisLabels,
  updateCameraState,
} from './ViscaAPI';
import { setToast } from '../components/Toast';
import {
  getCameraModel,
  setCameraModel,
  setCameraPresets,
  getLensRange,
  setLensRange,
  useLensRange,
  viscaScale,
  ExposureMode,
  FocusReach,
  LensRange,
  getFocusReach,
  setFocusReach,
  useCameraState,
  useFocusReach,
  useViscaIP,
  useViscaPort,
  useViscaState,
  zoomBand,
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

/** A quarter of the bar standing for full range that can't be reached, with a break mark. */
const Squeezed = () => (
  <Box
    aria-hidden
    sx={{
      width: '25%',
      flexShrink: 0,
      position: 'relative',
      height: 28,
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      color: 'text.disabled',
    }}
  >
    <Box
      sx={{
        position: 'absolute',
        left: 0,
        right: 0,
        top: '50%',
        height: 2,
        transform: 'translateY(-50%)',
        bgcolor: 'action.disabled',
      }}
    />
    <Box
      component="svg"
      viewBox="0 0 10 10"
      sx={{
        width: 12,
        height: 12,
        position: 'relative',
        bgcolor: 'background.paper',
      }}
    >
      <path
        d="M1 9 L5 1 M5 9 L9 1"
        stroke="currentColor"
        strokeWidth={1.5}
        fill="none"
      />
    </Box>
  </Box>
);

/**
 * The lens position, and when a range is qualified, a slider to send the lens straight to a
 * position. A direct position move runs at the camera's top speed. While it moves, the thumb
 * stays on the target and the mark follows the lens; onSet resolves with where the lens ended,
 * and ending short of the target is the lens limit. Ends known for the current zoom get the
 * middle of the bar, with the unreachable full range squeezed into broken-axis stretches at
 * the ends.
 */
const LensSlider: React.FC<{
  label: string;
  value: number;
  range?: { min: number; max: number };
  reach?: { low?: number; high?: number };
  onSet: (value: number) => Promise<number>;
  onLimit?: (value: number, end: 'low' | 'high') => void;
}> = ({ label, value, range, reach, onSet, onLimit }) => {
  const [drag, setDrag] = useState<number>();
  const [target, setTarget] = useState<number>();
  const [limit, setLimit] = useState<number>();
  const tolerance = range ? (range.max - range.min) * 0.005 : 0;
  // Only the latest move may report; an earlier one still finishing would clear its target.
  const move = useRef(0);

  const moveTo = async (goal: number) => {
    move.current += 1;
    const id = move.current;
    setLimit(undefined);
    setTarget(goal);
    const ended = await onSet(goal);
    if (id !== move.current) return;
    setTarget(undefined);
    if (Math.abs(ended - goal) > tolerance) {
      setLimit(ended);
      onLimit?.(ended, goal > ended ? 'high' : 'low');
    }
  };

  useEffect(() => {
    if (limit !== undefined && value !== limit) setLimit(undefined);
  }, [limit, value]);

  let status = `${value}`;
  if (target !== undefined) status = `${value} → ${target}`;
  else if (limit !== undefined) status = `${value} – lens limit`;

  const low = Math.max(range?.min ?? 0, reach?.low ?? -Infinity);
  // A slider needs some span; at full wide the X30 focus is a single position.
  const high = Math.max(
    low + 1,
    Math.min(range?.max ?? 0, reach?.high ?? Infinity),
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
          <Box sx={{ display: 'flex', alignItems: 'center' }}>
            {low > range.min && <Squeezed />}
            <Box sx={{ flex: 1, minWidth: 0, mx: 1 }}>
              <Slider
                size="small"
                aria-label={label}
                min={low}
                max={high}
                value={drag ?? target ?? Math.min(high, Math.max(low, value))}
                marks={
                  target === undefined
                    ? false
                    : [{ value: Math.min(high, Math.max(low, value)) }]
                }
                valueLabelDisplay="auto"
                sx={{
                  // Inline-block would add a baseline gap and lift the rail above the squeezed ends.
                  display: 'block',
                  // Taller than the thumb and orange, so the lens shows over the filled track too.
                  '& .MuiSlider-mark, & .MuiSlider-markActive': {
                    width: 3,
                    height: 22,
                    borderRadius: 1,
                    bgcolor: 'warning.main',
                    opacity: 1,
                  },
                }}
                onChange={(_event, v) => setDrag(v as number)}
                onChangeCommitted={(_event, v) => {
                  setDrag(undefined);
                  moveTo(v as number).catch((error) =>
                    setToast({
                      severity: 'error',
                      msg: `${label} move failed: ${error}`,
                    }),
                  );
                }}
              />
            </Box>
            {high < range.max && <Squeezed />}
          </Box>
          <Box
            sx={{
              position: 'relative',
              height: '1.5em',
              typography: 'caption',
              color: 'text.secondary',
              mt: -0.5,
            }}
          >
            <Box component="span" sx={{ position: 'absolute', left: 0 }}>
              {range.min}
            </Box>
            <Box component="span" sx={{ position: 'absolute', right: 0 }}>
              {range.max}
            </Box>
            {low > range.min && (
              <Box
                component="span"
                // Centered under the slider's left end: past the squeezed quarter and its margin.
                sx={{
                  position: 'absolute',
                  left: 'calc(25% + 8px)',
                  transform: 'translateX(-50%)',
                  color: 'text.primary',
                }}
              >
                {low}
              </Box>
            )}
            {high < range.max && (
              <Box
                component="span"
                sx={{
                  position: 'absolute',
                  right: 'calc(25% + 8px)',
                  transform: 'translateX(50%)',
                  color: 'text.primary',
                }}
              >
                {high}
              </Box>
            )}
          </Box>
        </>
      )}
    </Box>
  );
};

/** Whether a focus end learned at one zoom applies at another; ends shift across a band. */
const nearZoom = (zoom: number, learned?: number) => {
  return (
    learned !== undefined &&
    Math.abs(zoom - learned) <=
      (viscaScale.zoom.max - viscaScale.zoom.min) * 0.01
  );
};

/** Updates what's known about the focus reach in the zoom band of a zoom position. */
const updateReach = (
  zoom: number,
  change: (reach: FocusReach) => FocusReach,
) => {
  const band = zoomBand(zoom, viscaScale.zoom);
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
  const [seenRange] = useLensRange();
  // The range seen so far, or the VISCA scale for an axis that hasn't moved yet.
  const shown = (axis: keyof LensRange) => {
    const seen = seenRange?.[axis];
    return seen && seen.max > seen.min ? seen : viscaScale[axis];
  };
  const [focusReach] = useFocusReach();
  // The focus position at the last +/- press; unchanged a second later means a limit.
  const focusPress = useRef<{
    end: 'low' | 'high';
    from: number;
    at: number;
  }>();
  const bandReach = focusReach[zoomBand(cameraState.zoom, viscaScale.zoom)];
  // Only an end found at (nearly) this zoom applies; elsewhere in the band it may differ.
  const atZoom = (learned?: number) => nearZoom(cameraState.zoom, learned);
  const focusReachView = bandReach && {
    low: atZoom(bandReach.lowZoom) ? bandReach.low : undefined,
    high: atZoom(bandReach.highZoom) ? bandReach.high : undefined,
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
        const version = await sendViscaCommand({ type: 'VERSION_VALUE' });
        if (version.data?.[1] === 0x50) {
          const model = Array.from(version.data.slice(2, 6), (b) =>
            b.toString(16).padStart(2, '0'),
          ).join('');
          const stored = getCameraModel();
          if (model !== stored) {
            // Lens data learned before models were stored is assumed to be from this camera.
            if (stored !== undefined) {
              setLensRange(undefined);
              setFocusReach({});
              setCameraPresets([]);
            }
            setCameraModel(model);
          }
        }
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

  // Follow zoom and focus as they move, from the buttons, autofocus or a lens sweep. Not gated on
  // 'Connected': the state is only sent on change, so a reloaded window stays 'Idle' on a live link.
  useEffect(() => {
    if (!viscaIP || viscaPort === 0 || viscaState === 'Disconnected') {
      return undefined;
    }
    let stopped = false;
    // Each read waits for the last one, so slow replies can't pile up in the VISCA queue.
    const poll = async () => {
      try {
        const zoom = await getLensPosition('zoom');
        const focus = await getLensPosition('focus');
        // Autofocus too, so the Auto/Manual toggle shows what the camera is doing.
        const af = await sendViscaCommand({ type: 'AUTO_FOCUS_VALUE' });
        setCameraState((prev) => ({
          ...prev,
          zoom,
          focus,
          ...(af.data?.[1] === 0x50 && { autoFocus: af.data[2] === 2 }),
        }));
        const seen = getLensRange();
        if (
          !seen ||
          zoom < seen.zoom.min ||
          zoom > seen.zoom.max ||
          focus < seen.focus.min ||
          focus > seen.focus.max
        ) {
          setLensRange({
            zoom: {
              min: Math.min(seen?.zoom.min ?? zoom, zoom),
              max: Math.max(seen?.zoom.max ?? zoom, zoom),
            },
            focus: {
              min: Math.min(seen?.focus.min ?? focus, focus),
              max: Math.max(seen?.focus.max ?? focus, focus),
            },
          });
        }
        const press = focusPress.current;
        if (press && Date.now() - press.at > 1000) {
          focusPress.current = undefined;
          if (focus === press.from) {
            updateReach(zoom, (reach) => ({
              ...reach,
              [press.end]: focus,
              [`${press.end}Zoom`]: zoom,
            }));
          }
        }
        // An end learned at this zoom moves out to the new position (continuous moves reach a
        // little further than direct ones); one from elsewhere in the band no longer applies.
        updateReach(zoom, (reach) => {
          let next = reach;
          if (reach.low !== undefined && focus < reach.low) {
            const lowZoom = nearZoom(zoom, reach.lowZoom) ? zoom : undefined;
            next = { ...next, low: focus, lowZoom };
          }
          if (reach.high !== undefined && focus > reach.high) {
            const highZoom = nearZoom(zoom, reach.highZoom) ? zoom : undefined;
            next = { ...next, high: focus, highZoom };
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
  }, [setCameraState, viscaIP, viscaPort, viscaState]);

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

  // A big zoom change (the slider) hands focus back to the camera; a small tweak keeps it.
  const autoFocus = () => {
    setCameraState((prev) => ({ ...prev, autoFocus: true }));
    return sendViscaCommand({ type: 'AUTO_FOCUS', value: true });
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
                onPress={(direction) => {
                  focusPress.current = {
                    end: direction === 'up' ? 'high' : 'low',
                    from: cameraState.focus,
                    at: Date.now(),
                  };
                }}
              />
              <LensSlider
                label="Focus position"
                value={cameraState.focus}
                range={shown('focus')}
                reach={focusReachView}
                onLimit={(value, end) =>
                  updateReach(cameraState.zoom, (reach) => {
                    // A direct move stops short of where continuous moves already got.
                    const known = reach[end];
                    const further =
                      known !== undefined &&
                      (end === 'high' ? known > value : known < value);
                    if (
                      further &&
                      nearZoom(cameraState.zoom, reach[`${end}Zoom`])
                    ) {
                      return reach;
                    }
                    return {
                      ...reach,
                      [end]: value,
                      [`${end}Zoom`]: cameraState.zoom,
                    };
                  })
                }
                onSet={async (value) => {
                  // The camera ignores a focus position while autofocus is on.
                  setCameraState((prev) => ({ ...prev, autoFocus: false }));
                  await sendViscaCommand({ type: 'AUTO_FOCUS', value: false });
                  return driveFocus(value);
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
                range={shown('zoom')}
                onSet={async (value) => {
                  await autoFocus();
                  // A zoom move reports completion at once, so wait for the lens to stop.
                  await sendViscaCommand({ type: 'SET_ZOOM', value });
                  return settleLens('zoom');
                }}
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
    </Box>
  );
};

export default ViscaControlPanel;
