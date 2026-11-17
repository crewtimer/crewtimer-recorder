import React, { useEffect, useState } from 'react';
import {
  Alert,
  Box,
  Button,
  Checkbox,
  FormControlLabel,
  Paper,
  Step,
  StepContent,
  StepLabel,
  Stepper,
  TextField,
  Tooltip,
  Typography,
} from '@mui/material';
import {
  getGuide,
  getIsRecording,
  getRecordingProps,
  setGuide,
  useGuide,
  useHorizon,
  useRecordingProps,
} from '../recorder/RecorderData';
import { startRecording, stopRecording } from '../recorder/RecorderApi';
import { showErrorDialog } from '../components/ErrorDialog';
import { getVideoScaling, useVideoScaling } from '../util/VideoSettings';
import {
  setZoomCenters,
  setZoomCalibrationCamera,
  useZoomCalibrationCamera,
  useZoomCenters,
} from './ViscaState';

/**
 * Where the crop's center is, in source pixels from the frame's center. The timing line is
 * stored from the crop's center, but the zoom center belongs to the sensor, so it is kept
 * from the frame's center and stays right if the crop changes.
 */
const cropCenterOffset = () => {
  const { srcWidth } = getVideoScaling();
  const { cropArea } = getRecordingProps();
  return (cropArea.x + cropArea.width / 2) * srcWidth - srcWidth / 2;
};

/** The timing line's column in source pixels from the frame's center (its middle, if it leans). */
const guideColumn = () => {
  const { pt1, pt2 } = getGuide();
  return Math.round((pt1 + pt2) / 2 + cropCenterOffset());
};

/** Moves the timing line, restarting a recording so the recorder picks it up. */
const moveGuide = (guide: { pt1: number; pt2: number }) => {
  const prior = getGuide();
  if (guide.pt1 === prior.pt1 && guide.pt2 === prior.pt2) return;
  setGuide(guide);
  if (getIsRecording()) {
    stopRecording().catch(showErrorDialog);
    setTimeout(() => startRecording().catch(showErrorDialog), 100);
  }
};

/** Saves, or with undefined forgets, the zoom-center column of the camera at an endpoint. */
const saveZoomCenter = (endpoint: string, column: number | undefined) => {
  setZoomCenters((prior) => {
    const centers = { ...prior };
    if (column === undefined) delete centers[endpoint];
    else centers[endpoint] = { column, width: getVideoScaling().srcWidth };
    return centers;
  });
};

/** Puts the timing line upright on a column measured from the frame's center. */
const setGuideColumn = (column: number) => {
  const offset = Math.round(column - cropCenterOffset());
  moveGuide({ pt1: offset, pt2: offset });
};

/** Levelling, then the steps of Part 1 of the zoom calibration, after setting the camera up as for racing. */
const CalibrationSteps: React.FC<{ endpoint: string }> = ({ endpoint }) => {
  const onClose = () => setZoomCalibrationCamera(undefined);
  const [step, setStep] = useState(0);
  const [round, setRound] = useState(1);
  const [guide] = useGuide();
  const [videoScaling] = useVideoScaling();
  const [recordingProps, setRecordingProps] = useRecordingProps();
  const [horizon, setHorizon] = useHorizon();
  // Re-read on each render so the saved column follows the line as it is dragged.
  const column = guideColumn();
  const leans = guide.pt1 !== guide.pt2;

  const next = () => setStep((s) => s + 1);
  // Steps are found by id when clicked, after the list below is built.
  // eslint-disable-next-line no-use-before-define, @typescript-eslint/no-use-before-define
  const goTo = (id: string) => setStep(steps.findIndex((s) => s.id === id));
  const back = () => setStep((s) => Math.max(0, s - 1));
  const actions = (...buttons: React.ReactNode[]) => (
    <Box sx={{ display: 'flex', gap: 1, mt: 1, flexWrap: 'wrap' }}>
      {buttons}
      {step > 0 && (
        <Button size="small" onClick={back}>
          Back
        </Button>
      )}
    </Box>
  );
  const nextButton = (
    <Button key="next" size="small" variant="contained" onClick={next}>
      Next
    </Button>
  );

  const steps: { id: string; label: string; content: React.ReactNode }[] = [
    {
      id: 'level',
      label: 'Level the camera',
      content: (
        <>
          <Typography variant="body2">
            Adjust the side tilt so vertical objects run straight up and down.
            Adjust the forward tilt so the finish area is near the middle of the
            picture top to bottom. Do this first: tilting afterward could undo
            the alignment.
          </Typography>
          <Typography variant="body2" color="text.secondary" sx={{ mt: 0.5 }}>
            Tip: Enable the horizon line to verify level to a far shore feature
            such as a window or deck.
          </Typography>
          <FormControlLabel
            control={
              <Checkbox
                checked={horizon.enabled}
                onChange={() =>
                  setHorizon((prior) => ({
                    ...prior,
                    enabled: !prior.enabled,
                  }))
                }
                size="small"
              />
            }
            label="Show horizon"
            sx={{ m: 0, mt: 0.5 }}
          />
          {actions(nextButton)}
        </>
      ),
    },
    {
      id: 'center',
      label: 'Start with the timing line at the center',
      content: (
        <>
          <Typography variant="body2">
            You need a sharp, narrow, distant target, such as a pole, a flag
            edge or a building corner, and a firm tripod or mount.
          </Typography>
          {actions(
            <Button
              key="center"
              size="small"
              variant="contained"
              onClick={() => {
                if (!recordingProps.showFinishGuide) {
                  setRecordingProps({
                    ...recordingProps,
                    showFinishGuide: true,
                  });
                }
                moveGuide({ pt1: 0, pt2: 0 });
                next();
              }}
            >
              Center timing line
            </Button>,
          )}
        </>
      ),
    },
    {
      id: 'zoomIn',
      label: `Zoom in and aim${round > 1 ? ` (round ${round})` : ''}`,
      content: (
        <>
          <Typography variant="body2">
            Zoom in as far as you&apos;ll normally zoom during racing. Rotate
            the mount until the target is exactly on the timing line.
          </Typography>
          {actions(nextButton)}
        </>
      ),
    },
    {
      id: 'zoomOut',
      label: 'Zoom out',
      content: (
        <>
          <Typography variant="body2">
            Zoom out as far as you&apos;ll normally zoom out.{' '}
            <strong>Don&apos;t touch the mount.</strong> If the target is on the
            timing line, choose Alignment OK. Otherwise Next to move the timing
            line onto the target.
          </Typography>
          {actions(
            nextButton,
            // Still on the line: no need to move it, so go on to checking it stays on.
            <Button
              key="aligned"
              size="small"
              variant="outlined"
              onClick={() => goTo('check')}
            >
              Alignment OK
            </Button>,
          )}
        </>
      ),
    },
    {
      id: 'move',
      label: 'Move the timing line onto the target',
      content: (
        <>
          <Typography variant="body2">
            Drag the timing line in the preview onto the target&apos;s new
            position.
          </Typography>
          {actions(nextButton)}
        </>
      ),
    },
    {
      id: 'check',
      label: 'Check it stays on',
      content: (
        <>
          <Typography variant="body2">
            Zoom in again without touching the mount. Is the target still on the
            timing line? It usually takes 2 or 3 rounds.
          </Typography>
          {actions(
            <Button key="yes" size="small" variant="contained" onClick={next}>
              Yes, it stays on
            </Button>,
            <Button
              key="again"
              size="small"
              variant="outlined"
              onClick={() => {
                setRound((r) => r + 1);
                goTo('zoomIn');
              }}
            >
              No, go again
            </Button>,
          )}
        </>
      ),
    },
    {
      id: 'record',
      label: 'Record the zoom-center column',
      content: (
        <>
          <Typography variant="body2">
            The timing line is at <strong>{column} px</strong> from the center
            of the frame, measured at {videoScaling.srcWidth} px wide. Save it
            as this camera&apos;s zoom-center column.
          </Typography>
          {leans && (
            <Alert severity="warning" sx={{ mt: 1 }}>
              The timing line leans, so its middle is saved. Saving puts the
              line upright there.
            </Alert>
          )}
          {actions(
            <Button
              key="save"
              size="small"
              variant="contained"
              onClick={() => {
                saveZoomCenter(endpoint, column);
                setGuideColumn(column);
                onClose();
              }}
            >
              Save
            </Button>,
          )}
        </>
      ),
    },
  ];

  return (
    <Paper variant="outlined" sx={{ p: 1.5 }}>
      <Box sx={{ display: 'flex', alignItems: 'center', mb: 1 }}>
        <Typography variant="subtitle2" component="h2" sx={{ flex: 1 }}>
          Calibrate Zoom
        </Typography>
        <Button size="small" onClick={onClose}>
          Cancel
        </Button>
      </Box>
      <Stepper activeStep={step} orientation="vertical">
        {steps.map(({ id, label, content }) => (
          <Step key={id}>
            <StepLabel>{label}</StepLabel>
            <StepContent TransitionProps={{ unmountOnExit: true }}>
              {content}
            </StepContent>
          </Step>
        ))}
      </Stepper>
    </Paper>
  );
};

/**
 * The Calibrate Zoom steps, shown beside the camera controls while Calibrate Zoom is open.
 * Keyed by camera, so calibrating another camera starts again from the first step.
 */
export const ZoomCalibrationWizard: React.FC = () => {
  const [endpoint] = useZoomCalibrationCamera();
  return endpoint ? (
    <CalibrationSteps key={endpoint} endpoint={endpoint} />
  ) : null;
};

/**
 * Calibrate Zoom and the zoom-center column it finds for the camera at an endpoint. The column
 * can be typed in from a record, and Set line puts the timing line on it.
 */
const ZoomCalibration: React.FC<{ endpoint: string }> = ({ endpoint }) => {
  const [zoomCenters] = useZoomCenters();
  const [videoScaling] = useVideoScaling();
  const [calibrating] = useZoomCalibrationCamera();
  const open = calibrating === endpoint;
  const saved = zoomCenters[endpoint];
  const [text, setText] = useState(saved ? `${saved.column}` : '');
  useEffect(() => setText(saved ? `${saved.column}` : ''), [saved]);

  const save = (column: number | undefined) => saveZoomCenter(endpoint, column);
  const commit = () => {
    const column = Math.round(Number(text));
    if (text.trim() === '') save(undefined);
    else if (Number.isFinite(column)) save(column);
    else setText(saved ? `${saved.column}` : '');
  };
  // A column scales with the frame, so one from another resolution would be in the wrong place.
  const otherWidth =
    saved && videoScaling.srcWidth > 1 && saved.width !== videoScaling.srcWidth
      ? saved.width
      : undefined;

  // 36 px tall, as the other camera controls are.
  return (
    <Box>
      <Box
        sx={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: 1 }}
      >
        <Tooltip
          describeChild
          placement="left"
          title="Calibrate the camera lens center offset, allowing a target on the timing line to stay on it as you zoom"
        >
          <Button
            size="small"
            variant="outlined"
            onClick={() => setZoomCalibrationCamera(endpoint)}
            disabled={open}
            sx={{ height: 36, whiteSpace: 'nowrap', flexShrink: 0 }}
          >
            Calibrate Zoom
          </Button>
        </Tooltip>
        <Tooltip
          describeChild
          placement="left"
          title="Zoom-center column, in source pixels from the center of the frame"
        >
          <TextField
            size="small"
            type="text"
            label="Zoom center"
            value={text}
            onChange={(event) => setText(event.target.value)}
            onBlur={commit}
            onKeyDown={(event) => {
              if (event.key === 'Enter') commit();
            }}
            error={otherWidth !== undefined}
            inputProps={{ inputMode: 'numeric' }}
            sx={{ width: 96, '& .MuiInputBase-root': { height: 36 } }}
          />
        </Tooltip>
        <Tooltip
          describeChild
          placement="left"
          title="Put the timing line on the zoom-center column"
        >
          <span>
            <Button
              size="small"
              disabled={!saved}
              sx={{ height: 36, whiteSpace: 'nowrap' }}
              onClick={() => saved && setGuideColumn(saved.column)}
            >
              Set line
            </Button>
          </span>
        </Tooltip>
      </Box>
      {otherWidth !== undefined && (
        <Typography
          variant="caption"
          color="error"
          component="p"
          sx={{ mt: 0.5 }}
        >
          Measured at {otherWidth} px wide; this video is{' '}
          {videoScaling.srcWidth} px wide
        </Typography>
      )}
    </Box>
  );
};

export default ZoomCalibration;
