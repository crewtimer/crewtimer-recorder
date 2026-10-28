import { useEffect } from 'react';
import { UseDatum } from 'react-usedatum';
import { KeyMap, Lap } from 'crewtimer-common';
import { firebaseSubscribe } from '../util/UseFirebase';
import { addSystemEvent, requestSplitAt } from './RecorderApi';
import { getIsRecording, useRecordingProps } from './RecorderData';
import {
  gateFromWaypoint,
  lapTimeToMilli,
  regattaPathId,
  waypointsFromInfo,
} from './LapTime';

interface RegattaInfo {
  Title?: string;
  Waypoints?: string;
}

export interface RegattaState {
  status: 'none' | 'loading' | 'found' | 'missing';
  title: string;
  /** Start, the regatta's waypoints, then Finish */
  waypoints: string[];
  /** Time of day of the last crossing received at the waypoint */
  lastTime: string;
  splitCount: number;
}

const NoRegatta: RegattaState = {
  status: 'none',
  title: '',
  waypoints: [],
  lastTime: '',
  splitCount: 0,
};

export const [useRegattaState, setRegattaState, getRegattaState] =
  UseDatum<RegattaState>(NoRegatta);

/**
 * Why the recorder will not split on the regatta's times, or undefined when
 * it will (or is still looking the regatta up).
 */
export const regattaProblem = (
  regattaId: string,
  waypoint: string,
  state: RegattaState,
) => {
  if (!regattaId.trim()) return 'Regatta ID is not set';
  if (state.status === 'missing')
    return `Regatta ${regattaId.trim()} not found`;
  if (!waypoint) return 'Hint Waypoint is not set';
  return undefined;
};

const log = (message: string) =>
  addSystemEvent({ tsMilli: Date.now(), subsystem: 'regatta', message });

/** Split for a new crossing while recording; a closed file may already hold it. */
const splitForLap = (lap: Lap, tsMilli: number, waypoint: string) => {
  setRegattaState({ ...getRegattaState(), lastTime: lap.Time || '' });
  if (!getIsRecording()) return;
  requestSplitAt(tsMilli)
    .then((result) => {
      if (result?.status === 'OK') {
        const state = getRegattaState();
        setRegattaState({ ...state, splitCount: state.splitCount + 1 });
        log(`Split for ${waypoint} time ${lap.Time}`);
      } else if (result?.status === 'Ignored') {
        log(
          `${waypoint} time ${lap.Time} is in a file that is closed or closing`,
        );
      }
      return result;
    })
    .catch((err) => {
      log(`Error: Could not split for ${waypoint} time ${lap.Time}: ${err}`);
    });
};

/**
 * Watches the configured regatta: its info for the Source tab, and its times
 * at the recorder's waypoint, splitting the recording for each new crossing.
 */
export const RegattaMonitor = () => {
  const [recordingProps, setRecordingProps] = useRecordingProps();
  const { regattaId, waypoint } = recordingProps;
  const id = (regattaId || '').trim();
  const [regatta] = useRegattaState();

  // A regatta's times need one of its waypoints; Any or another regatta's
  // waypoint picks Finish.
  useEffect(() => {
    if (regatta.status !== 'found' || regatta.waypoints.includes(waypoint)) {
      return;
    }
    setRecordingProps({ ...recordingProps, waypoint: 'Finish' });
    log(`Hint waypoint set to Finish for ${regatta.title || id}`);
  }, [regatta, waypoint, recordingProps, setRecordingProps, id]);

  useEffect(() => {
    if (!id) {
      setRegattaState(NoRegatta);
      return undefined;
    }
    setRegattaState({ ...NoRegatta, status: 'loading' });
    return firebaseSubscribe<RegattaInfo, RegattaInfo>(
      `web/${regattaPathId(id)}/regattaInfo`,
      (info) => {
        setRegattaState({
          ...getRegattaState(),
          status: info ? 'found' : 'missing',
          title: info?.Title || '',
          waypoints: info ? waypointsFromInfo(info.Waypoints) : [],
        });
      },
      { regattaId: id },
    );
  }, [id]);

  useEffect(() => {
    if (!id || !waypoint) return undefined;
    // Times already there when listening starts are recorded; only later ones split.
    let seen: Set<string> | undefined;
    return firebaseSubscribe<KeyMap<Lap>, KeyMap<Lap>>(
      `regatta/${regattaPathId(id)}/lapdata`,
      (lapMap) => {
        const laps = Object.entries(lapMap || {}).map(([key, lap]) => ({
          // Editing a time, e.g. adding its bow, keeps its uuid
          crossing: lap.uuid || key,
          lap,
        }));
        if (!seen) {
          seen = new Set(laps.map(({ crossing }) => crossing));
          return;
        }
        const nowMilli = Date.now();
        laps.forEach(({ crossing, lap }) => {
          if (seen?.has(crossing) || lap.State === 'Deleted') return;
          const tsMilli = lapTimeToMilli(lap.Time, nowMilli);
          if (tsMilli === undefined) return;
          seen?.add(crossing);
          splitForLap(lap, tsMilli, waypoint);
        });
      },
      {
        regattaId: id,
        filter: { key: 'Gate', value: gateFromWaypoint(waypoint) },
      },
    );
  }, [id, waypoint]);

  return null;
};
