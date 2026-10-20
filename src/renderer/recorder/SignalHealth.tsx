import { ReactNode } from 'react';
import { Box, LinearProgress, Typography } from '@mui/material';
import { alpha } from '@mui/material/styles';
import { monoFont, Panel } from '../components/Panel';
import {
  useCameraTimeSample,
  useRecordingDiskUsage,
  useRecordingStatus,
  useSystemLog,
} from './RecorderData';
import { hasCameraTimeMismatch } from './CameraTime';
import { eventLevel, isGapEvent } from '../pages/RecordingLogTable';
import { RecordingLogEntry } from './RecorderTypes';

// FrameProcessor discards its queue beyond this many frames
const MAX_BACKLOG = 500;
// Recording disk usage at which to warn; matches CrewTimer Video Review
export const DISK_WARN_PERCENT = 85;
// A running reader with no video frame for this long is not connected
const FRAME_ARRIVAL_TIMEOUT_MS = 3000;

export const formatBytes = (bytes: number) =>
  bytes >= 1e12
    ? `${(bytes / 1e12).toFixed(1)} TB`
    : `${Math.round(bytes / 1e9)} GB`;

export type Tone = 'success' | 'warning' | 'error' | undefined;

const Metric = ({
  label,
  value,
  note,
  tone,
  children,
}: {
  label: string;
  value: string;
  note?: string;
  tone?: Tone;
  children?: ReactNode;
}) => (
  <Box
    sx={{
      p: 1.25,
      borderRadius: 1,
      border: 1,
      borderColor: (theme) =>
        tone === 'warning' || tone === 'error'
          ? alpha(theme.palette[tone].main, 0.5)
          : theme.palette.divider,
      bgcolor: (theme) =>
        tone === 'warning' || tone === 'error'
          ? alpha(theme.palette[tone].main, 0.1)
          : theme.palette.background.default,
      display: 'flex',
      flexDirection: 'column',
      gap: 0.5,
    }}
  >
    <Typography variant="caption" color="text.secondary">
      {label}
    </Typography>
    <Typography sx={{ fontFamily: monoFont, fontSize: 20 }}>{value}</Typography>
    {note && (
      <Typography variant="caption" color={tone ? `${tone}.main` : undefined}>
        {note}
      </Typography>
    )}
    {children}
  </Box>
);

export const formatLag = (ms: number) =>
  Math.abs(ms) < 10000
    ? `${ms >= 0 ? '+' : ''}${Math.round(ms)} ms`
    : `${ms >= 0 ? '+' : ''}${(ms / 1000).toFixed(1)} s`;

const total = (entries: RecordingLogEntry[]) =>
  entries.reduce((sum, e) => sum + (e.count ?? 1), 0);

/** Live metrics and their warning levels, shared by the health panel and status bar. */
export const useSignalStats = () => {
  const [status] = useRecordingStatus();
  const [timeSample] = useCameraTimeSample();
  const [log] = useSystemLog();
  const [disk] = useRecordingDiskUsage();

  const {
    fps,
    measuredFps,
    clockOffsetMs,
    lastTsMilli,
    frameBacklog,
    width,
    height,
    filename,
    lowFrameRate,
  } = status.frameProcessor;
  // More than 1% off the camera's declared rate means frames are missing or extra
  const fpsOff = measuredFps > 0 && Math.abs(measuredFps - fps) > 0.01 * fps;
  const gaps = log.filter(isGapEvent);
  // While recording, the native module measures the offset where frames arrive;
  // otherwise fall back to the preview frame sampled by the UI.
  const offsetAtArrival = status.recording && measuredFps > 0;
  let offset: number | undefined;
  let clockOff = false;
  if (offsetAtArrival) {
    offset = clockOffsetMs;
    clockOff = hasCameraTimeMismatch(lastTsMilli, lastTsMilli - clockOffsetMs);
  } else if (timeSample) {
    offset = timeSample.cameraTime - timeSample.systemTime;
    clockOff = hasCameraTimeMismatch(
      timeSample.cameraTime,
      timeSample.systemTime,
    );
  }
  let backlogTone: Tone = 'success';
  if (frameBacklog > 200) backlogTone = 'error';
  else if (frameBacklog > 100) backlogTone = 'warning';
  const diskUsage = disk && !disk.error ? disk : undefined;
  // Whether video is actually arriving; discovery stops while recording, so
  // the camera list alone would miss a camera that drops mid-recording
  const connected =
    status.readerRunning &&
    Date.now() - status.lastFrameArrivalMilli < FRAME_ARRIVAL_TIMEOUT_MS;
  // Compare the rounded value so the percentage shown and its colour agree
  const diskFull =
    !!diskUsage && Math.round(diskUsage.usedPercent) >= DISK_WARN_PERCENT;

  return {
    recording: status.recording,
    connected,
    filename,
    fps,
    measuredFps,
    fpsOff,
    lowFrameRate,
    diskUsage,
    diskFull,
    width,
    height,
    frameBacklog,
    backlogTone,
    // PC arrival time minus camera frame time: the network/pipeline delay when
    // the clocks agree, so it reads as a small positive number.
    arrivalLag: offset === undefined ? undefined : -offset,
    offsetAtArrival,
    clockOff,
    gapCount: total(gaps),
    errorCount: total(log.filter((e) => eventLevel(e) === 'Error')),
  };
};

export const SignalHealth = ({ connected }: { connected: boolean }) => {
  const {
    recording,
    fps,
    measuredFps,
    fpsOff,
    width,
    height,
    frameBacklog,
    backlogTone,
    arrivalLag,
    offsetAtArrival,
    clockOff,
    lowFrameRate,
  } = useSignalStats();

  if (!connected) {
    return (
      <Panel title="Signal health">
        <Typography variant="body2" color="text.secondary">
          Shows frame rate, camera clock offset and frame gaps once a camera is
          connected.
        </Typography>
      </Panel>
    );
  }

  const warnings = [clockOff, fpsOff, backlogTone !== 'success'].filter(
    Boolean,
  ).length;

  return (
    <Panel
      title="Signal health"
      aside={
        warnings > 0 && (
          <Typography
            color="warning.main"
            sx={{ fontSize: 12, fontWeight: 600 }}
          >
            {`${warnings} warning${warnings > 1 ? 's' : ''}`}
          </Typography>
        )
      }
    >
      <Box
        sx={{
          display: 'grid',
          gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
          gap: 1.25,
        }}
      >
        <Metric
          label="Frame rate (actual)"
          value={recording && measuredFps ? measuredFps.toFixed(2) : '—'}
          note={
            recording && fps
              ? `${lowFrameRate ? 'low data rate, ' : ''}target ${fps.toFixed(2)}`
              : 'while recording'
          }
          tone={(lowFrameRate && 'error') || (fpsOff && 'warning') || undefined}
        />
        <Metric
          label="Arrival lag"
          value={arrivalLag === undefined ? '—' : formatLag(arrivalLag)}
          note={
            clockOff
              ? 'check camera time'
              : `${offsetAtArrival ? '' : 'preview, '}network delay + clock offset`
          }
          tone={clockOff ? 'error' : undefined}
        />
        <Metric
          label="Pixel rate"
          value={
            recording && measuredFps
              ? `${Math.round((width * height * measuredFps) / 1e6)} Mpx/s`
              : '—'
          }
          note={recording ? `${width}×${height}` : 'while recording'}
        />
        <Metric
          label="Encoder backlog"
          value={recording ? `${frameBacklog} frames` : '—'}
          tone={backlogTone === 'success' ? undefined : backlogTone}
        >
          <LinearProgress
            variant="determinate"
            color={backlogTone}
            value={Math.min(100, (100 * frameBacklog) / MAX_BACKLOG)}
            sx={{ height: 4, borderRadius: 2 }}
          />
        </Metric>
      </Box>
    </Panel>
  );
};
