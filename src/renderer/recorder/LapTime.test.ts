import {
  gateFromWaypoint,
  lapTimeToMilli,
  regattaPathId,
  waypointsFromInfo,
} from './LapTime';

const local = (
  y: number,
  mo: number,
  d: number,
  h: number,
  mi: number,
  s = 0,
) => new Date(y, mo - 1, d, h, mi, s).getTime();

describe('lapTimeToMilli', () => {
  it('places a time of day on the current day', () => {
    const now = local(2026, 6, 14, 10, 30);
    expect(lapTimeToMilli('10:29:58.250', now)).toBe(
      local(2026, 6, 14, 10, 29, 58) + 250,
    );
  });

  it('keeps a crossing from just before midnight on the previous day', () => {
    const now = local(2026, 6, 15, 0, 0, 2);
    expect(lapTimeToMilli('23:59:59.000', now)).toBe(
      local(2026, 6, 14, 23, 59, 59),
    );
  });

  it('moves a crossing just after midnight to the next day', () => {
    const now = local(2026, 6, 14, 23, 59, 59);
    expect(lapTimeToMilli('00:00:01', now)).toBe(local(2026, 6, 15, 0, 0, 1));
  });

  it('rejects text that is not a time', () => {
    const now = local(2026, 6, 14, 10, 30);
    expect(lapTimeToMilli(undefined, now)).toBeUndefined();
    expect(lapTimeToMilli('', now)).toBeUndefined();
    expect(lapTimeToMilli('10:61:00', now)).toBeUndefined();
    expect(lapTimeToMilli('103000', now)).toBeUndefined();
  });
});

describe('gateFromWaypoint', () => {
  it('maps Start and Finish to their gates and others to G_ names', () => {
    expect(gateFromWaypoint('Start')).toBe('S');
    expect(gateFromWaypoint('Finish')).toBe('F');
    expect(gateFromWaypoint('500m')).toBe('G_500m');
  });
});

describe('waypointsFromInfo', () => {
  it('wraps the regatta waypoints in Start and Finish', () => {
    expect(waypointsFromInfo(' 500m, 1000m ,')).toEqual([
      'Start',
      '500m',
      '1000m',
      'Finish',
    ]);
    expect(waypointsFromInfo(undefined)).toEqual(['Start', 'Finish']);
  });
});

describe('regattaPathId', () => {
  it('removes the dev database prefix', () => {
    expect(regattaPathId('t.r16305')).toBe('r16305');
    expect(regattaPathId('9r16305')).toBe('r16305');
    expect(regattaPathId('r16305')).toBe('r16305');
  });
});
