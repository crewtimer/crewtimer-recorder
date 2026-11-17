import '@testing-library/jest-dom';
import React from 'react';
import { act, fireEvent, render, screen } from '@testing-library/react';
import ZoomCalibration, { ZoomCalibrationWizard } from './ZoomCalibration';
import { setVideoScaling, getVideoScaling } from '../util/VideoSettings';
import { ZoomCenter } from './ViscaState';

jest.mock('../recorder/RecorderData', () => {
  // eslint-disable-next-line @typescript-eslint/no-var-requires, global-require
  const { UseDatum: Datum } = require('react-usedatum');
  const [useGuide, setGuideDatum, getGuideDatum] = Datum({ pt1: 0, pt2: 0 });
  const [useHorizon] = Datum({ enabled: false, yPct: 0.5 });
  const [useRecordingProps, setProps, getRecordingProps] = Datum({
    cropArea: { x: 0, y: 0, width: 1, height: 1 },
    showFinishGuide: false,
  });
  return {
    useGuide,
    setGuide: setGuideDatum,
    getGuide: getGuideDatum,
    useHorizon,
    useRecordingProps,
    setRecordingProps: setProps,
    getRecordingProps,
    getIsRecording: () => false,
  };
});
jest.mock('../recorder/RecorderApi', () => ({
  startRecording: jest.fn(),
  stopRecording: jest.fn(),
}));
jest.mock('../components/ErrorDialog', () => ({ showErrorDialog: jest.fn() }));
jest.mock('./ViscaState', () => {
  // eslint-disable-next-line @typescript-eslint/no-var-requires, global-require
  const { UseDatum: Datum } = require('react-usedatum');
  const [useZoomCenters, setZoomCenters, getZoomCenters] = Datum({});
  const [useZoomCalibrationCamera, setZoomCalibrationCamera] = Datum(undefined);
  return {
    useZoomCenters,
    setZoomCenters,
    getZoomCenters,
    useZoomCalibrationCamera,
    setZoomCalibrationCamera,
  };
});

type Guide = { pt1: number; pt2: number };
const recorder = jest.requireMock('../recorder/RecorderData') as {
  getGuide: () => Guide;
  setGuide: (guide: Guide) => void;
  setRecordingProps: (props: object) => void;
};
const visca = jest.requireMock('./ViscaState') as {
  getZoomCenters: () => Record<string, ZoomCenter>;
  setZoomCenters: (centers: Record<string, ZoomCenter>) => void;
  setZoomCalibrationCamera: (endpoint: string | undefined) => void;
};
const { getGuide } = recorder;
const { getZoomCenters } = visca;
/** Changes shared state after rendering, as a drag or another control would. */
const setGuide = (guide: Guide) => act(() => recorder.setGuide(guide));
const setWidth = (srcWidth: number) =>
  act(() => setVideoScaling({ ...getVideoScaling(), srcWidth }));

const endpoint = '10.0.0.5:52381';
/** The controls, and the wizard that opens in its own column beside them. */
const renderBoth = () =>
  render(
    <>
      <ZoomCalibration endpoint={endpoint} />
      <ZoomCalibrationWizard />
    </>,
  );
/** Clicks a button, then lets the step transitions finish so only the open step is mounted. */
const click = (name: string | RegExp) => {
  fireEvent.click(screen.getByRole('button', { name }));
  act(() => {
    jest.runOnlyPendingTimers();
  });
};

beforeEach(() => {
  jest.useFakeTimers();
  visca.setZoomCenters({});
  visca.setZoomCalibrationCamera(undefined);
  recorder.setGuide({ pt1: 0, pt2: 0 });
  recorder.setRecordingProps({
    cropArea: { x: 0, y: 0, width: 1, height: 1 },
    showFinishGuide: false,
  });
  setVideoScaling({ ...getVideoScaling(), srcWidth: 1920 });
});

afterEach(() => {
  jest.useRealTimers();
});

describe('Calibrate Zoom', () => {
  it('steps through calibration, going round again, and saves the column', () => {
    renderBoth();
    setGuide({ pt1: 40, pt2: 40 });
    click('Calibrate Zoom');
    click('Next'); // level the camera
    click('Center timing line');
    expect(getGuide()).toEqual({ pt1: 0, pt2: 0 });
    click('Next'); // zoom in and aim
    click('Next'); // zoom out
    setGuide({ pt1: 12, pt2: 12 }); // dragged onto the target
    click('Next');
    click('No, go again');
    expect(screen.getByText('Zoom in and aim (round 2)')).toBeInTheDocument();
    click('Next');
    click('Next');
    setGuide({ pt1: 15, pt2: 15 });
    click('Next');
    click('Yes, it stays on');
    expect(screen.getByText('15 px')).toBeInTheDocument();
    click('Save');
    expect(getZoomCenters()[endpoint]).toEqual({ column: 15, width: 1920 });
    expect(screen.queryByText('Record the zoom-center column')).toBeNull();
    expect(
      screen.getByRole('button', { name: 'Calibrate Zoom' }),
    ).toBeEnabled();
    expect(screen.getByLabelText('Zoom center')).toHaveValue('15');
  });

  it('starts by levelling, with the horizon line to hand', () => {
    renderBoth();
    click('Calibrate Zoom');
    expect(screen.getByText('Level the camera')).toBeInTheDocument();
    const horizon = screen.getByRole('checkbox', { name: 'Show horizon' });
    expect(horizon).not.toBeChecked();
    fireEvent.click(horizon);
    expect(horizon).toBeChecked();
    click('Next');
    expect(
      screen.getByRole('button', { name: 'Center timing line' }),
    ).toBeInTheDocument();
  });

  it('skips moving the line when the target is still on it zoomed out', () => {
    renderBoth();
    click('Calibrate Zoom');
    click('Next'); // level the camera
    click('Center timing line');
    click('Next'); // zoom in and aim
    click('Alignment OK');
    expect(screen.getByText(/Is the target still on the/)).toBeInTheDocument();
  });

  it('keeps the column from the frame center when the crop is off center', () => {
    // The crop's center is at (0.35 + 0.25) * 1920 = 1152 px, 192 px right of the frame's.
    recorder.setRecordingProps({
      cropArea: { x: 0.35, y: 0, width: 0.5, height: 1 },
      showFinishGuide: true,
    });
    visca.setZoomCenters({ [endpoint]: { column: 200, width: 1920 } });
    renderBoth();
    click('Set line');
    expect(getGuide()).toEqual({ pt1: 8, pt2: 8 });
  });

  it('saves the middle of a leaning line and stands it upright', () => {
    renderBoth();
    click('Calibrate Zoom');
    click('Next'); // level the camera
    click('Center timing line');
    ['Next', 'Next', 'Next', 'Yes, it stays on'].forEach(click);
    setGuide({ pt1: 10, pt2: 20 });
    click('Back');
    click('Yes, it stays on');
    expect(screen.getByText(/leans/)).toBeInTheDocument();
    click('Save');
    expect(getZoomCenters()[endpoint].column).toBe(15);
    expect(getGuide()).toEqual({ pt1: 15, pt2: 15 });
  });

  it('takes a typed column and warns when it was measured at another width', () => {
    renderBoth();
    const field = screen.getByLabelText('Zoom center');
    fireEvent.change(field, { target: { value: '-37' } });
    fireEvent.blur(field);
    expect(getZoomCenters()[endpoint]).toEqual({ column: -37, width: 1920 });
    // Without a number input, anything can be typed; what isn't a number goes back.
    fireEvent.change(field, { target: { value: 'abc' } });
    fireEvent.blur(field);
    expect(field).toHaveValue('-37');
    expect(getZoomCenters()[endpoint].column).toBe(-37);
    setWidth(3840);
    expect(
      screen.getByText(/Measured at 1920 px wide; this video is 3840/),
    ).toBeInTheDocument();
  });
});
