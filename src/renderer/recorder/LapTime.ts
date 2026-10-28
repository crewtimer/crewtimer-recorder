const DAY_MILLI = 24 * 3600 * 1000;

/**
 * Converts a lap's local time of day ('HH:MM:SS.sss') to UTC milliseconds on
 * the day nearest nowMilli, so a crossing just before midnight that arrives
 * after it lands on the previous day, and the reverse.  Returns undefined when
 * the time cannot be parsed.
 */
export const lapTimeToMilli = (
  time: string | undefined,
  nowMilli: number,
): number | undefined => {
  const match = /^\s*(\d{1,2}):(\d{1,2}):(\d{1,2}(?:\.\d*)?)\s*$/.exec(
    time || '',
  );
  if (!match) {
    return undefined;
  }
  const [hours, minutes, seconds] = match.slice(1).map(Number);
  if (hours > 23 || minutes > 59 || seconds >= 60) {
    return undefined;
  }
  const day = new Date(nowMilli);
  day.setHours(0, 0, 0, 0);
  // setHours handles a DST change during the day; seconds keep their fraction
  day.setHours(hours, minutes, 0, 0);
  let milli = day.getTime() + Math.round(seconds * 1000);
  if (milli - nowMilli > DAY_MILLI / 2) milli -= DAY_MILLI;
  else if (nowMilli - milli > DAY_MILLI / 2) milli += DAY_MILLI;
  return milli;
};

/**
 * Regatta id as used in database paths. The t. or 9 prefix only selects the
 * dev database, and a . is not allowed in a path.
 */
export const regattaPathId = (regattaId: string) =>
  regattaId.replace(/^(t\.|9)/, '');

/** Lap Gate for a waypoint name: 'Start' -> 'S', 'Finish' -> 'F', else 'G_<name>'. */
export const gateFromWaypoint = (waypoint: string) => {
  if (waypoint === 'Start') return 'S';
  if (waypoint === 'Finish') return 'F';
  return `G_${waypoint}`;
};

/** Start, the regatta's comma separated Waypoints, then Finish. */
export const waypointsFromInfo = (waypoints: string | undefined) => [
  'Start',
  ...(waypoints || '')
    .split(',')
    .map((wp) => wp.trim())
    .filter((wp) => wp),
  'Finish',
];
