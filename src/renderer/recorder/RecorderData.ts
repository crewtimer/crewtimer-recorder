import { UseDatum } from 'react-usedatum';

import { UseStoredDatum } from '../store/UseElectronDatum';
import generateTestPattern from '../util/ImageUtils';
import {
  DefaultRecordingProps,
  DefaultRecordingStatus,
  GrabFrameResponse,
  RecordingLogEntry,
  RecordingProps,
  RecordingStatus,
} from './RecorderTypes';

export const [useRecordingPropsPending, setRecordingPropsPending] =
  UseDatum(false);
export const [useRecordingProps, _setRecordingProps, getRecordingProps] =
  UseStoredDatum<RecordingProps>('record', DefaultRecordingProps);

window.Util.getDocumentsFolder()
  .then((folder) => {
    if (DefaultRecordingProps.recordingFolder === './') {
      DefaultRecordingProps.recordingFolder = folder;
    }
    const recordingProps = getRecordingProps();
    if (recordingProps.recordingFolder === './') {
      _setRecordingProps({ ...recordingProps, recordingFolder: folder });
    }
    return '';
  })
  .catch(() => {});
export const [
  useRecordingStartTime,
  setRecordingStartTime,
  getRecordingStartTime,
] = UseDatum(0);
export const [useIsRecording, setIsRecording, getIsRecording] = UseDatum(false);

export const [useRecordingStatus, setRecordingStatus] =
  UseDatum<RecordingStatus>(DefaultRecordingStatus);

export const [useFrameGrab, setFrameGrab, getFrameGrab] = UseDatum<
  GrabFrameResponse | undefined
>(generateTestPattern());

export const [useCameraTimeSample, setCameraTimeSample, getCameraTimeSample] =
  UseDatum<
    | {
        camera: string;
        protocol: string;
        cameraTime: number;
        systemTime: number;
      }
    | undefined
  >(undefined);

export const [useGuide, setGuide, getGuide] = UseStoredDatum('guide', {
  pt1: 0,
  pt2: 0,
});

export const [useSystemLog, setSystemLog, getSystemLog] = UseDatum<
  RecordingLogEntry[]
>([]);

export const [useReportAllGaps, , getReportAllGaps] = UseStoredDatum(
  'reportAllGaps',
  false,
);

/** Shows the signal health panel on the Recorder tab. */
export const [useNerdMode] = UseStoredDatum('nerdMode', false);

export const [useWaypointList, setWaypointList, getWaypointList] = UseDatum<
  string[]
>([]);
export const [useWaypoint] = UseStoredDatum('waypoint', '');

export interface FocusProps {
  enabled: boolean;
  xPct: number;
  yPct: number;
  sizePct: number;
}

export const [useFocusArea] = UseStoredDatum<FocusProps>('focusArea', {
  enabled: false,
  xPct: 0.5,
  yPct: 0.5,
  sizePct: 0.2,
});

/** Horizontal reference line on the preview, as a fraction of the frame height. */
export const [useHorizon] = UseStoredDatum('horizon', {
  enabled: false,
  yPct: 0.5,
});
