import { useEffect, useRef } from 'react';
import {
  useActiveRecordingFolder,
  useIsRecording,
  useRecordingProps,
  setRecordingDiskUsage,
} from './RecorderData';
import { addSystemEvent } from './RecorderApi';
import { DISK_WARN_PERCENT, formatBytes, useSignalStats } from './SignalHealth';
import { useRegattaState } from './RegattaMonitor';

const STATUS_INTERVAL_MS = 60 * 1000;
const DISK_POLL_MS = 60 * 1000;

/**
 * Polls recording disk usage and multicasts a status report every minute.
 * Warnings and errors are multicast as they occur by addSystemEvent.
 */
export const StatusReporter = () => {
  const [{ networkCamera, protocol, recordingFolder, waypoint, regattaId }] =
    useRecordingProps();
  const [regatta] = useRegattaState();
  // Splitting on the regatta's times, so Video Review need not ask for splits
  const regattaListening = regatta.status === 'found' && !!waypoint;
  const [isRecording] = useIsRecording();
  const [activeFolder] = useActiveRecordingFolder();
  const stats = useSignalStats();
  // While recording, watch the disk being written, not a folder setting
  // edited since; the recorder keeps writing to the original until restarted
  const diskFolder =
    isRecording && activeFolder ? activeFolder : recordingFolder;
  const currentDiskFolder = useRef(diskFolder);
  currentDiskFolder.current = diskFolder;

  // The interval reads the latest values rather than restarting on each change
  const report = useRef<object>({});
  report.current = {
    cmd: 'recorder-status',
    wp: waypoint,
    regattaId: (regattaId || '').trim(),
    regattaListening,
    version: window.platform.appVersion,
    camera: networkCamera,
    protocol,
    connected: stats.connected,
    recording: stats.recording,
    filename: stats.filename,
    // The values shown in the Signal health (nerd mode) panel
    signalHealth: {
      fps: stats.fps,
      measuredFps: stats.measuredFps,
      lowFrameRate: stats.lowFrameRate,
      width: stats.width,
      height: stats.height,
      pixelRateMpx: Math.round(
        (stats.width * stats.height * stats.measuredFps) / 1e6,
      ),
      frameBacklog: stats.frameBacklog,
      arrivalLagMs:
        stats.arrivalLag === undefined ? null : Math.round(stats.arrivalLag),
      clockOff: stats.clockOff,
    },
    gapCount: stats.gapCount,
    errorCount: stats.errorCount,
    disk: stats.diskUsage
      ? {
          folder: diskFolder,
          usedPercent: Math.round(stats.diskUsage.usedPercent),
          freeBytes: stats.diskUsage.freeBytes,
          totalBytes: stats.diskUsage.totalBytes,
        }
      : null,
  };

  const sendReport = () =>
    window.Util.multicastSend({ ...report.current, tsMilli: Date.now() });

  // Report at startup and as soon as recording starts or stops or the camera
  // connection or regatta changes, so listeners need not wait for the next minute
  useEffect(() => {
    sendReport();
  }, [stats.recording, stats.connected, regattaListening]);

  useEffect(() => {
    const timer = setInterval(sendReport, STATUS_INTERVAL_MS);
    return () => clearInterval(timer);
  }, []);

  const diskWarned = useRef(false);
  useEffect(() => {
    diskWarned.current = false;
    const poll = () => {
      window.Util.getDiskUsage(diskFolder)
        .then((usage) => {
          // A folder change may land while this poll was in flight
          if (currentDiskFolder.current !== diskFolder) {
            return undefined;
          }
          setRecordingDiskUsage(usage);
          if (usage.error) return undefined;
          const full = Math.round(usage.usedPercent) >= DISK_WARN_PERCENT;
          if (full && !diskWarned.current) {
            addSystemEvent({
              tsMilli: Date.now(),
              subsystem: 'disk',
              message: `Warning: Recording disk ${Math.round(usage.usedPercent)}% full (${formatBytes(usage.freeBytes)} free)`,
            });
          }
          diskWarned.current = full;
          return undefined;
        })
        .catch(() => {});
    };
    poll();
    const timer = setInterval(poll, DISK_POLL_MS);
    return () => clearInterval(timer);
  }, [diskFolder]);

  return null;
};

export default StatusReporter;
