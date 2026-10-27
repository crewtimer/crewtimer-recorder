import React, { useEffect, useRef, useState } from 'react';
import {
  Alert,
  Box,
  Button,
  Checkbox,
  CircularProgress,
  FormControlLabel,
  TextField,
  Typography,
  MenuItem,
  Tooltip,
  IconButton,
  Stack,
  ToggleButton,
  ToggleButtonGroup,
} from '@mui/material';
import FolderOpenIcon from '@mui/icons-material/FolderOpen';
import OpenInNewIcon from '@mui/icons-material/OpenInNew';
import VideocamOffOutlinedIcon from '@mui/icons-material/VideocamOffOutlined';
import {
  useRecordingStatus,
  useRecordingProps,
  useRecordingPropsPending,
  useWaypointList,
  useIsRecording,
  getIsRecording,
  useNerdMode,
  useFrameGrab,
} from './RecorderData';
import { FullSizeWindow } from '../components/FullSizeWindow';
import PreviewCanvas from '../components/PreviewCanvas';
import { showErrorDialog } from '../components/ErrorDialog';
import { Panel } from '../components/Panel';
import { SignalHealth } from './SignalHealth';
import ViscaControlPanel from '../visca/ViscaControlPanel';
import { useSelectedPage } from '../pages/SelectedPage';
import { useViscaIP } from '../visca/ViscaState';
import {
  refreshCameraList,
  useCameraList,
  useInitialCameraSearchComplete,
} from './CameraMonitor';
import { ViscaPortSelector } from '../visca/ViscaPortSelector';
import RotationSelector from './RotationSelector';
import { startPreview, stopPreview, updateSettings } from './RecorderApi';
import { DefaultRecordingProps } from './RecorderTypes';
import {
  CAMERA_FALLBACK_IP,
  showCameraFallbackDialog,
} from './CameraFallbackDialog';

const { openDirDialog, openFileExplorer } = window.Util;
const isMac = window.platform.platform === 'darwin';

type Camera = { name: string; address: string };
const cameraLabel = (camera: Camera) =>
  `${camera.name.replace(camera.address, '').replace('-)', ')')} — ${camera.address}`;

const RecordingError = () => {
  const [recordingStatus] = useRecordingStatus();
  return recordingStatus.error ? (
    <Alert severity="error">{recordingStatus.error}</Alert>
  ) : null;
};

/** Quick-pick buttons for cameras that were found instead of the selected one. */
const CameraChoices: React.FC<{
  cameras: Camera[];
  onSelect: (name: string) => void;
}> = ({ cameras, onSelect }) => (
  <>
    {cameras.slice(0, 4).map((camera) => (
      <Button
        key={camera.name}
        variant="outlined"
        onClick={() => onSelect(camera.name)}
      >
        {`Use ${cameraLabel(camera)}`}
      </Button>
    ))}
  </>
);

/** Spinner and headline filling the preview area while something is in progress. */
const BusyMessage: React.FC<{ title: string; children?: React.ReactNode }> = ({
  title,
  children,
}) => (
  <Box
    role="status"
    sx={{
      height: '100%',
      display: 'flex',
      flexDirection: 'column',
      alignItems: 'center',
      justifyContent: 'center',
      gap: 2,
      p: 4,
      textAlign: 'center',
    }}
  >
    <CircularProgress />
    <Typography variant="h6" component="h2">
      {title}
    </Typography>
    {children}
  </Box>
);

/** How long a newly started preview may take to deliver its first frame. */
const FIRST_FRAME_TIMEOUT_MS = 10000;
/** A running stream with no new frame for this long has stopped. */
const FRAME_STALL_TIMEOUT_MS = 3000;

/** Give up waiting on a requested rescan after this long. */
const RESCAN_TIMEOUT_MS = 10000;

const NoCamera: React.FC<{
  selectedCamera: string;
  cameras: Camera[];
  onSelect: (name: string) => void;
  /** The camera is on the network but its video stopped. */
  noVideo?: boolean;
}> = ({ selectedCamera, cameras, onSelect, noVideo }) => {
  // Discovery is slow on first launch; avoid flashing the macOS permission hint
  const [hintReady, setHintReady] = useState(false);
  const [searching, setSearching] = useState(false);
  useEffect(() => {
    if (!searching) return undefined;
    // Poll until the native side reports the requested scan has finished.
    const started = Date.now();
    let cancelled = false;
    let timer: ReturnType<typeof setTimeout> | undefined;
    const poll = () => {
      timer = setTimeout(async () => {
        const pending = await refreshCameraList();
        if (cancelled) return;
        if (pending && Date.now() - started < RESCAN_TIMEOUT_MS) {
          poll();
        } else {
          setSearching(false);
        }
      }, 1000);
    };
    poll();
    return () => {
      cancelled = true;
      clearTimeout(timer);
    };
  }, [searching]);
  useEffect(() => {
    const timer = setTimeout(() => setHintReady(true), 6000);
    return () => clearTimeout(timer);
  }, []);

  return (
    <Box
      sx={{
        height: '100%',
        border: '1px dashed',
        borderColor: 'divider',
        borderRadius: 1,
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        justifyContent: 'center',
        gap: 2,
        p: 4,
        textAlign: 'center',
      }}
    >
      <VideocamOffOutlinedIcon sx={{ fontSize: 56, color: 'text.secondary' }} />
      <Typography variant="h6" component="h2">
        {noVideo && `No video from ${selectedCamera}`}
        {!noVideo &&
          (selectedCamera
            ? `${selectedCamera} isn't on the network`
            : 'No camera selected')}
      </Typography>
      <Typography color="text.secondary" sx={{ maxWidth: 540 }}>
        {isMac
          ? 'Check that the camera is powered and on the same network as this Mac, and that Local Network access is allowed for this app.'
          : 'Check that the camera is powered and on the same network as this PC, and that Windows Firewall allows NDI and SRT.'}{' '}
        {noVideo
          ? 'The recorder reconnects automatically when video returns.'
          : 'Cameras are searched for every 5 seconds.'}
      </Typography>
      <Stack direction="row" gap={1} flexWrap="wrap" justifyContent="center">
        <Button
          variant="contained"
          disabled={searching}
          onClick={() => {
            setSearching(true);
            refreshCameraList(true);
          }}
        >
          {searching ? 'Searching…' : 'Search again'}
        </Button>
        {isMac && hintReady && cameras.length === 0 && (
          <Button
            variant="outlined"
            onClick={() =>
              window.open(
                'x-apple.systempreferences:com.apple.preference.security?Privacy_LocalNetwork',
                '_blank',
              )
            }
          >
            Open Local Network settings
          </Button>
        )}
        <CameraChoices cameras={cameras} onSelect={onSelect} />
      </Stack>
    </Box>
  );
};

const RecorderConfig: React.FC<{ showPreview?: boolean }> = ({
  showPreview = true,
}) => {
  const [recordingProps, setRecordingProps] = useRecordingProps();
  const [, setRecordingPropsPending] = useRecordingPropsPending();
  const [cameraList] = useCameraList();
  const [initialSearchComplete] = useInitialCameraSearchComplete();
  const [isRecording] = useIsRecording();
  const [viscaIP] = useViscaIP();
  const [nerdMode] = useNerdMode();
  const [page] = useSelectedPage();
  const [wpList] = useWaypointList();
  const { waypoint } = recordingProps;
  const waypointList = [...wpList];
  if (waypoint && !waypointList.includes(waypoint)) {
    waypointList.push(waypoint);
  }

  useEffect(() => {
    updateSettings({ waypoint });
  }, [waypoint]);

  const chooseDir = () => {
    openDirDialog('Choose Video Folder', recordingProps.recordingFolder)
      .then((result) => {
        if (!result.cancelled) {
          setRecordingPropsPending(true);
          setRecordingProps({
            ...recordingProps,
            recordingFolder: result.path,
          });
        }
        return null;
      })
      .catch(showErrorDialog);
  };

  const handleChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    const value =
      event.target.type === 'checkbox'
        ? event.target.checked
        : event.target.value;

    if (
      [
        'recordingDuration',
        'recordingPrefix',
        'recordingQuality',
        'overlapRecording',
      ].includes(event.target.name)
    ) {
      setRecordingPropsPending(true);
    }
    setRecordingProps({
      ...recordingProps,
      [event.target.name]:
        event.target.name === 'recordingQuality' ? Number(value) : value,
    });
  };

  const selectCamera = (name: string) => {
    setRecordingPropsPending(true);
    setRecordingProps({
      ...recordingProps,
      networkCamera: name,
    });
    const camera = cameraList.find((c) => c.name === name);
    if (camera?.address === CAMERA_FALLBACK_IP) {
      showCameraFallbackDialog();
    }
  };

  const selectProtocol = (protocol: string | null) => {
    if (!protocol) return;
    setRecordingPropsPending(true);
    setRecordingProps({ ...recordingProps, protocol });
  };

  const selectedCamera = recordingProps.networkCamera;
  const camFound = cameraList.some((c) => c.name === selectedCamera);

  // After the stream drops the native side keeps returning its last frame, so a camera
  // timestamp that stops advancing is what shows the video is gone.
  const [frame] = useFrameGrab();
  const [noVideo, setNoVideo] = useState(false);
  const [connecting, setConnecting] = useState(false);
  // The preview restarts whenever one of these changes; give each new connection
  // longer to deliver its first frame than a running stream gets between frames.
  const connectionKey = `${camFound}|${selectedCamera}|${recordingProps.protocol}`;
  const connection = useRef({
    key: '',
    startTs: undefined as number | undefined,
  });
  useEffect(() => {
    setNoVideo(false);
    if (!camFound) {
      setConnecting(false);
      return undefined;
    }
    if (connection.current.key !== connectionKey) {
      connection.current = { key: connectionKey, startTs: frame?.tsMilli };
    }
    const awaitingFirstFrame = frame?.tsMilli === connection.current.startTs;
    setConnecting(awaitingFirstFrame);
    const timer = setTimeout(
      () => setNoVideo(true),
      awaitingFirstFrame ? FIRST_FRAME_TIMEOUT_MS : FRAME_STALL_TIMEOUT_MS,
    );
    return () => clearTimeout(timer);
  }, [frame?.tsMilli, connectionKey, camFound]);

  useEffect(() => {
    if (isRecording) {
      return () => {};
    }

    if (camFound && selectedCamera) {
      startPreview().catch(showErrorDialog);
    } else {
      stopPreview().catch(showErrorDialog);
    }

    return () => {
      if (!getIsRecording()) {
        stopPreview().catch(showErrorDialog);
      }
    };
  }, [isRecording, recordingProps.protocol, selectedCamera, camFound]);

  // Rotation is applied per frame by the native side, so a running preview takes
  // the new value without reconnecting. While recording it waits for a restart.
  const lastRotation = useRef(recordingProps.rotation);
  useEffect(() => {
    if (lastRotation.current === recordingProps.rotation) return;
    lastRotation.current = recordingProps.rotation;
    if (!isRecording && camFound && selectedCamera) {
      startPreview().catch(showErrorDialog);
    }
  }, [recordingProps.rotation, isRecording, camFound, selectedCamera]);

  const restartNote = isRecording && (
    <Typography variant="caption" color="text.secondary" noWrap>
      Applies when recording restarts
    </Typography>
  );

  return (
    <Box
      sx={{ display: 'flex', flexDirection: 'column', gap: 2, height: '100%' }}
    >
      <RecordingError />
      <Box
        sx={{
          flex: 1,
          minHeight: 0,
          display: 'flex',
          // Side by side, preview filling the height, when there is room; stacked otherwise
          flexWrap: { xs: 'wrap', md: 'nowrap' },
          gap: 2.5,
          alignItems: 'stretch',
        }}
      >
        <Box
          component="section"
          aria-label="Live preview"
          sx={{
            flex: '999 1 440px',
            minWidth: 0,
            minHeight: { xs: 360, md: 0 },
            // Contain the absolutely positioned preview canvas
            position: 'relative',
            overflow: 'hidden',
          }}
        >
          {camFound ? (
            showPreview && <FullSizeWindow component={PreviewCanvas} />
          ) : !initialSearchComplete ? (
            <BusyMessage
              title={
                selectedCamera
                  ? `Searching for ${selectedCamera}…`
                  : 'Searching for cameras…'
              }
            >
              {cameraList.length > 0 && (
                <>
                  <Typography color="text.secondary">
                    {selectedCamera
                      ? 'Other cameras found so far:'
                      : 'Cameras found so far:'}
                  </Typography>
                  <Stack
                    direction="row"
                    gap={1}
                    flexWrap="wrap"
                    justifyContent="center"
                  >
                    <CameraChoices
                      cameras={cameraList}
                      onSelect={selectCamera}
                    />
                  </Stack>
                </>
              )}
            </BusyMessage>
          ) : (
            <NoCamera
              selectedCamera={selectedCamera}
              cameras={cameraList}
              onSelect={selectCamera}
            />
          )}
          {/* These cover the preview rather than replacing it: the preview is what polls for frames */}
          {camFound && connecting && !noVideo && (
            <Box
              sx={{
                position: 'absolute',
                inset: 0,
                bgcolor: 'background.default',
              }}
            >
              <BusyMessage title={`Connecting to ${selectedCamera}…`} />
            </Box>
          )}
          {camFound && noVideo && (
            <Box
              sx={{
                position: 'absolute',
                inset: 0,
                bgcolor: 'background.default',
              }}
            >
              <NoCamera
                selectedCamera={selectedCamera}
                cameras={cameraList}
                onSelect={selectCamera}
                noVideo
              />
            </Box>
          )}
        </Box>

        <Box
          component="aside"
          aria-label="Settings and health"
          sx={{
            flex: '1 1 320px',
            maxWidth: 400,
            overflowY: { md: 'auto' },
            display: 'flex',
            flexDirection: 'column',
            gap: 2,
          }}
        >
          {nerdMode && <SignalHealth connected={camFound} />}

          {!['/recording', '/source'].includes(page) && <ViscaControlPanel />}

          {page === '/source' && (
            <Panel title="Source" aside={restartNote}>
              <TextField
                select
                label="Camera"
                size="small"
                value={selectedCamera}
                onChange={(e) => selectCamera(e.target.value)}
                error={initialSearchComplete && !camFound && !!selectedCamera}
                helperText={
                  !initialSearchComplete
                    ? 'Searching for cameras…'
                    : !camFound && selectedCamera
                      ? 'Not found on the network. Searching every 5 s.'
                      : undefined
                }
                fullWidth
              >
                {cameraList.map((c) => (
                  <MenuItem key={c.name} value={c.name}>
                    {cameraLabel(c)}
                  </MenuItem>
                ))}
                {!camFound && selectedCamera && (
                  <MenuItem value={selectedCamera}>
                    {`${selectedCamera} — ${initialSearchComplete ? 'not found' : 'searching…'}`}
                  </MenuItem>
                )}
              </TextField>
              <Stack direction="row" alignItems="flex-end" gap={1.5}>
                <Box sx={{ flex: 1 }}>
                  <Typography
                    variant="caption"
                    color="text.secondary"
                    component="div"
                    id="protocol-label"
                  >
                    Protocol
                  </Typography>
                  <ToggleButtonGroup
                    exclusive
                    fullWidth
                    size="small"
                    color="primary"
                    aria-labelledby="protocol-label"
                    value={recordingProps.protocol}
                    onChange={(_, value) => selectProtocol(value)}
                    sx={{ height: 40 }}
                  >
                    <ToggleButton value="NDI">NDI</ToggleButton>
                    <ToggleButton value="SRT">SRT</ToggleButton>
                  </ToggleButtonGroup>
                </Box>
                <ViscaPortSelector />
              </Stack>
              <Box>
                <Typography
                  variant="caption"
                  color="text.secondary"
                  component="div"
                >
                  Rotation
                </Typography>
                <RotationSelector />
              </Box>
              {camFound && (
                <Button
                  size="small"
                  endIcon={<OpenInNewIcon fontSize="small" />}
                  onClick={() => window.open(`http://${viscaIP}`)}
                  sx={{ alignSelf: 'flex-start' }}
                >
                  {`Open camera web page (${viscaIP})`}
                </Button>
              )}
            </Panel>
          )}

          {page === '/recording' && (
            <Panel title="Recording" aside={restartNote}>
              <TextField
                label="Folder"
                size="small"
                fullWidth
                value={recordingProps.recordingFolder}
                onClick={chooseDir}
                InputProps={{ readOnly: true }}
              />
              <Stack direction="row" spacing={0.75} alignItems="center">
                <Button
                  variant="outlined"
                  onClick={chooseDir}
                  sx={{ flexShrink: 0 }}
                >
                  Change…
                </Button>
                <Tooltip title="Open folder">
                  <IconButton
                    aria-label="Open recording folder"
                    onClick={() =>
                      openFileExplorer(recordingProps.recordingFolder)
                    }
                  >
                    <FolderOpenIcon />
                  </IconButton>
                </Tooltip>
              </Stack>
              <Box
                sx={{
                  display: 'grid',
                  gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
                  gap: 1.5,
                }}
              >
                <TextField
                  size="small"
                  label="File prefix"
                  name="recordingPrefix"
                  value={recordingProps.recordingPrefix}
                  onChange={handleChange}
                />
                <TextField
                  size="small"
                  label="Slice length (s)"
                  name="recordingDuration"
                  type="number"
                  value={String(recordingProps.recordingDuration)}
                  onChange={handleChange}
                />
                <Tooltip
                  placement="top"
                  title="Higher quality uses more disk space. Applies on next start."
                >
                  <TextField
                    select
                    size="small"
                    label="Quality"
                    name="recordingQuality"
                    value={recordingProps.recordingQuality ?? 80}
                    onChange={handleChange}
                  >
                    <MenuItem value={60}>Standard</MenuItem>
                    <MenuItem value={70}>Medium</MenuItem>
                    <MenuItem value={80}>High</MenuItem>
                    <MenuItem value={90}>Very High</MenuItem>
                  </TextField>
                </Tooltip>
                <Tooltip
                  placement="top"
                  title="Bind this recorder to a Video Review waypoint"
                >
                  <TextField
                    select
                    size="small"
                    label="Waypoint"
                    value={waypoint || 'Any'}
                    onChange={(e) =>
                      setRecordingProps({
                        ...recordingProps,
                        waypoint:
                          e.target.value === 'Any' ? '' : e.target.value,
                      })
                    }
                  >
                    <MenuItem value="Any">Any</MenuItem>
                    {waypointList.map((wp) => (
                      <MenuItem key={wp} value={wp}>
                        {wp}
                      </MenuItem>
                    ))}
                  </TextField>
                </Tooltip>
              </Box>
              <Tooltip
                placement="top"
                title="Each new file also starts with the last 2 seconds of the previous file, so Video Review can follow a finish across a file split. Applies on next start."
              >
                <FormControlLabel
                  control={
                    <Checkbox
                      name="overlapRecording"
                      checked={
                        recordingProps.overlapRecording ??
                        DefaultRecordingProps.overlapRecording
                      }
                      onChange={handleChange}
                      size="small"
                    />
                  }
                  label="Overlapped recording"
                  sx={{ m: 0, alignSelf: 'flex-start' }}
                />
              </Tooltip>
            </Panel>
          )}
        </Box>
      </Box>
    </Box>
  );
};

export default RecorderConfig;
