import { useEffect, useState } from 'react';
import { MenuItem, Stack, TextField, Tooltip, Typography } from '@mui/material';
import { Panel } from '../components/Panel';
import { useRecordingProps, useWaypointList } from './RecorderData';
import { useRegattaState } from './RegattaMonitor';

/** Regatta and hint waypoint whose times split the recording. */
export const RegattaSettings = () => {
  const [recordingProps, setRecordingProps] = useRecordingProps();
  const [regatta] = useRegattaState();
  const [seenWaypoints] = useWaypointList();
  const { waypoint } = recordingProps;
  const regattaId = recordingProps.regattaId || '';
  const [draftId, setDraftId] = useState(regattaId);

  useEffect(() => setDraftId(regattaId), [regattaId]);

  const commitId = () => {
    const id = draftId.trim();
    if (id !== regattaId)
      setRecordingProps({ ...recordingProps, regattaId: id });
  };

  // The regatta's waypoints once it loads, otherwise those heard from Video Review
  const waypointList = [
    ...(regatta.status === 'found' ? regatta.waypoints : seenWaypoints),
  ];
  if (waypoint && !waypointList.includes(waypoint)) {
    waypointList.push(waypoint);
  }

  let regattaText = '';
  if (regatta.status === 'loading') regattaText = 'Looking up regatta…';
  else if (regatta.status === 'missing') regattaText = 'Regatta not found';
  else if (regatta.status === 'found') regattaText = regatta.title || regattaId;

  let timesText = '';
  if (regatta.status === 'found') {
    if (!waypoint) {
      timesText = 'Choose a hint waypoint to split on its times.';
    } else {
      timesText = `Listening for ${waypoint} times`;
      if (regatta.lastTime) {
        timesText += ` · last ${regatta.lastTime} · ${regatta.splitCount} ${
          regatta.splitCount === 1 ? 'split' : 'splits'
        }`;
      }
    }
  }

  return (
    <Panel title="Regatta">
      <Stack direction="row" gap={1.5} alignItems="flex-start">
        <Tooltip
          placement="top"
          title="Each new time at the waypoint closes the current file so Video Review can open it straight away"
        >
          <TextField
            size="small"
            label="Regatta ID"
            id="regattaId"
            value={draftId}
            onChange={(e) => setDraftId(e.target.value)}
            onBlur={commitId}
            onKeyDown={(e) => {
              if (e.key === 'Enter') commitId();
            }}
            error={regatta.status === 'missing'}
            sx={{ flex: 1 }}
          />
        </Tooltip>
        <Tooltip
          placement="top"
          title="Split on regatta times at this waypoint. Set it to the timing hint waypoint in Video Review, which then sends this recorder its split requests and lists its new files at once."
        >
          <TextField
            select
            size="small"
            label="Hint Waypoint"
            id="waypoint"
            value={waypoint || 'Any'}
            onChange={(e) =>
              setRecordingProps({
                ...recordingProps,
                waypoint: e.target.value === 'Any' ? '' : e.target.value,
              })
            }
            sx={{ minWidth: 160 }}
          >
            {regatta.status !== 'found' && <MenuItem value="Any">Any</MenuItem>}
            {waypointList.map((wp) => (
              <MenuItem key={wp} value={wp}>
                {wp}
              </MenuItem>
            ))}
          </TextField>
        </Tooltip>
      </Stack>
      {regattaText && (
        <Typography
          variant="body2"
          color={regatta.status === 'missing' ? 'error' : 'text.primary'}
        >
          {regattaText}
        </Typography>
      )}
      {timesText && (
        <Typography variant="body2" color="text.secondary">
          {timesText}
        </Typography>
      )}
    </Panel>
  );
};
