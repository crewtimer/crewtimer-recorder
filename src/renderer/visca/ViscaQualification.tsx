/* eslint-disable no-await-in-loop */
import React, { useState } from 'react';
import {
  Box,
  Button,
  CircularProgress,
  Stack,
  Typography,
} from '@mui/material';
import {
  getCameraState,
  getLensPosition,
  sendViscaCommand,
  updateCameraState,
  ViscaCommand,
} from './ViscaAPI';
import { LensRange, setFocusReach, useLensRange, zoomBand } from './ViscaState';
import { setToast } from '../components/Toast';
import { snooze } from '../util/Util';

type Axis = keyof LensRange;
type Direction = 'up' | 'down';

const motion: Record<
  Axis,
  {
    up: ViscaCommand;
    down: ViscaCommand;
    stop: ViscaCommand;
    set: (value: number) => ViscaCommand;
  }
> = {
  zoom: {
    up: { type: 'ZOOM_IN' },
    down: { type: 'ZOOM_OUT' },
    stop: { type: 'ZOOM_RESET' },
    set: (value) => ({ type: 'SET_ZOOM', value }),
  },
  // Near raises the reported focus position, as in Sony VISCA.
  focus: {
    up: { type: 'FOCUS_NEAR' },
    down: { type: 'FOCUS_FAR' },
    stop: { type: 'FOCUS_RESET' },
    set: (value) => ({ type: 'SET_FOCUS', value }),
  },
};

/** Waits for the position to stop changing: at the commanded target or at the end of travel. */
const settle = async (axis: Axis) => {
  // A full zoom or focus run at the default speed takes about a minute.
  const deadline = Date.now() + 120000;
  let last = -1;
  for (;;) {
    await snooze(500);
    const pos = await getLensPosition(axis);
    if (pos === last) return pos;
    if (Date.now() > deadline) throw new Error(`${axis} never stopped moving`);
    last = pos;
  }
};

const runToEnd = async (axis: Axis, dir: Direction) => {
  await sendViscaCommand(motion[axis][dir]);
  try {
    return await settle(axis);
  } finally {
    await sendViscaCommand(motion[axis].stop);
  }
};

/** Finds all four ends, or with a saved range, starts just short of each saved end and drives into it. */
const measure = async (
  saved: LensRange | undefined,
  onStep: (step: string) => void,
): Promise<LensRange> => {
  const end = async (axis: Axis, dir: Direction) => {
    onStep(
      `${axis === 'zoom' ? 'Zoom' : 'Focus'} to ${dir === 'up' ? 'max' : 'min'}`,
    );
    if (saved) {
      const { min, max } = saved[axis];
      const margin = Math.round((max - min) * 0.02);
      await sendViscaCommand(
        motion[axis].set(dir === 'up' ? max - margin : min + margin),
      );
      await settle(axis);
    }
    return runToEnd(axis, dir);
  };
  const zoom = { min: await end('zoom', 'down'), max: await end('zoom', 'up') };
  // Focus travel depends on zoom (almost none at wide on the X30), so measure it at max zoom.
  const focus = {
    min: await end('focus', 'down'),
    max: await end('focus', 'up'),
  };
  return { zoom, focus };
};

const ViscaQualification: React.FC = () => {
  const [saved, setSaved] = useLensRange();
  const [step, setStep] = useState<string>();
  const [verified, setVerified] = useState<LensRange>();

  const qualify = async () => {
    const original = await getCameraState();
    try {
      await sendViscaCommand({ type: 'AUTO_FOCUS', value: false });
      const result = await measure(saved, setStep);
      if (saved) {
        setVerified(result);
      } else {
        setSaved(result);
        // The sweep ends focus at max zoom, so those ends are known for the top zoom band.
        setFocusReach({
          [zoomBand(result.zoom.max, result.zoom)]: {
            low: result.focus.min,
            high: result.focus.max,
            lowZoom: result.zoom.max,
            highZoom: result.zoom.max,
          },
        });
      }
    } finally {
      setStep('Restoring zoom and focus');
      await updateCameraState({
        autoFocus: original.autoFocus,
        zoom: original.zoom,
        focus: original.focus,
      });
    }
  };

  const onClick = async () => {
    setVerified(undefined);
    try {
      await qualify();
    } catch (error) {
      setToast({
        severity: 'error',
        msg: `Lens qualification failed: ${error}`,
      });
    }
    setStep(undefined);
  };

  const cell = (axis: Axis, end: 'min' | 'max') => {
    if (!saved) return '–';
    const expected = saved[axis][end];
    if (!verified) return expected;
    const actual = verified[axis][end];
    const ok =
      Math.abs(actual - expected) <= (saved[axis].max - saved[axis].min) * 0.01;
    return (
      <Box component="span" sx={{ color: ok ? 'success.main' : 'error.main' }}>
        {ok ? `${expected} ✓` : `${expected} → ${actual} ✗`}
      </Box>
    );
  };

  return (
    <Stack spacing={1}>
      <Box
        sx={{
          display: 'grid',
          gridTemplateColumns: 'auto auto auto',
          columnGap: 2,
          rowGap: 0.5,
          typography: 'body2',
        }}
      >
        <span />
        <Typography variant="caption" color="text.secondary">
          Min
        </Typography>
        <Typography variant="caption" color="text.secondary">
          Max
        </Typography>
        <span>Zoom</span>
        <span>{cell('zoom', 'min')}</span>
        <span>{cell('zoom', 'max')}</span>
        <span>Focus (max zoom)</span>
        <span>{cell('focus', 'min')}</span>
        <span>{cell('focus', 'max')}</span>
      </Box>
      <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
        <Button
          variant="outlined"
          size="small"
          disabled={!!step}
          onClick={onClick}
        >
          {saved ? 'Verify lens range' : 'Sweep lens range'}
        </Button>
        <Button
          size="small"
          disabled={!!step || !saved}
          onClick={() => {
            setSaved(undefined);
            setFocusReach({});
            setVerified(undefined);
          }}
        >
          Forget range
        </Button>
      </Box>
      {step && (
        <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
          <CircularProgress size={16} />
          <Typography variant="body2">{step}</Typography>
        </Box>
      )}
    </Stack>
  );
};

export default ViscaQualification;
