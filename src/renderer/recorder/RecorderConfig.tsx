import React, { useEffect, useState } from 'react';
import {
  Alert,
  Box,
  Button,
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
} from './RecorderData';
import { FullSizeWindow } from '../components/FullSizeWindow';
import PreviewCanvas from '../components/PreviewCanvas';
import { showErrorDialog } from '../components/ErrorDialog';
import { Panel } from '../components/Panel';
import { SignalHealth } from './SignalHealth';
import ViscaControlPanel from '../visca/ViscaControlPanel';
import { useSelectedPage } from '../pages/SelectedPage';
import { useViscaIP } from '../visca/ViscaState';
import { refreshCameraList, useCameraList } from './CameraMonitor';
import { ViscaPortSelector } from '../visca/ViscaPortSelector';
import { startPreview, stopPreview, updateSettings } from './RecorderApi';
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

const NoCamera: React.FC<{
  selectedCamera: string;
  cameras: Camera[];
  onSelect: (name: string) => void;
}> = ({ selectedCamera, cameras, onSelect }) => {
  // Discovery is slow on first launch; avoid flashing the macOS permission hint
  const [hintReady, setHintReady] = useState(false);
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
        {selectedCamera
          ? `${selectedCamera} isn't on the network`
          : 'No camera selected'}
      </Typography>
      <Typography color="text.secondary" sx={{ maxWidth: 540 }}>
        {isMac
          ? 'Check that the camera is powered and on the same network as this Mac, and that Local Network access is allowed for this app.'
          : 'Check that the camera is powered and on the same network as this PC, and that Windows Firewall allows NDI and SRT.'}{' '}
        Cameras are searched for every 5 seconds.
      </Typography>
      <Stack direction="row" gap={1} flexWrap="wrap" justifyContent="center">
        <Button variant="contained" onClick={refreshCameraList}>
          Search again
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
        {cameras.slice(0, 4).map((camera) => (
          <Button
            key={camera.name}
            variant="outlined"
            onClick={() => onSelect(camera.name)}
          >
            {`Use ${cameraLabel(camera)}`}
          </Button>
        ))}
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
      ['recordingDuration', 'recordingPrefix', 'recordingQuality'].includes(
        event.target.name,
      )
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
  }, [
    isRecording,
    recordingProps.protocol,
    recordingProps.rotation,
    selectedCamera,
    camFound,
  ]);

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
          ) : (
            <NoCamera
              selectedCamera={selectedCamera}
              cameras={cameraList}
              onSelect={selectCamera}
            />
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
                error={!camFound && !!selectedCamera}
                helperText={
                  !camFound && selectedCamera
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
                    {`${selectedCamera} — not found`}
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
            </Panel>
          )}
        </Box>
      </Box>
    </Box>
  );
};

export default RecorderConfig;
