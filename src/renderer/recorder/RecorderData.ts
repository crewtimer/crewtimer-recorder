import { UseDatum } from 'react-usedatum';

import { UseStoredDatum } from '../store/UseElectronDatum';
import generateTestPattern from '../util/ImageUtils';
import {
  DefaultRecordingProps,
  DefaultRecordingStatus,
  GrabFrameResponse,
  RecordingProps,
  RecordingStatus,
} from './RecorderTypes';

export const [useRecordingProps, _setRecordingProps, getRecordingProps] =
  UseStoredDatum<RecordingProps>('recording', DefaultRecordingProps);

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
export const [useIsRecording, setIsRecording] = UseDatum(false);

export const [useRecordingStatus, setRecordingStatus] =
  UseDatum<RecordingStatus>(DefaultRecordingStatus);

export const [useFrameGrab, setFrameGrab] = UseDatum<
  GrabFrameResponse | undefined
>(generateTestPattern());

export const [useGuide, setGuide] = UseStoredDatum('guide', { pt1: 0, pt2: 0 });
