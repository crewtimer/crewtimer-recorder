import {
  Typography,
  Box,
  Button,
  Stack,
  Link,
  Tooltip,
  Tabs,
  Tab,
} from '@mui/material';
import Visibility from '@mui/icons-material/Visibility';
import logo from '../../../assets/icons/64x64.png';
import HamburgerMenu from './HamburgerMenu';
import RecordingStatus from '../recorder/RecordingStatus';
import { useFirebaseDatum } from '../util/UseFirebase';
import { setDialogConfig } from './ConfirmDialog';
import { StartButton } from './StartButton';
import { useRecordingProps, useReportAllGaps } from '../recorder/RecorderData';
import { SETTINGS_PAGES, useSelectedPage } from '../pages/SelectedPage';

const versionAsNumber = (version: string) => {
  const parts = version.split('.');
  return Number(parts[0]) * 100 + Number(parts[1]) * 10 + Number(parts[2]);
};

export function TopBar() {
  const [page, setSelectedPage] = useSelectedPage();
  const [reportAllGaps] = useReportAllGaps();
  const [{ waypoint }] = useRecordingProps();
  const latestVersion =
    useFirebaseDatum<string, string>(
      '/global/config/video-recorder/latestVersion',
    ) || '0.0.0';
  const latestText =
    useFirebaseDatum<string, string>(
      '/global/config/video-recorder/latestText',
    ) || '';
  const updateAvailable =
    versionAsNumber(latestVersion) >
    versionAsNumber(window.platform.appVersion);

  // Help and Privacy are reached from the menu and select no tab
  let tab: string | false = false;
  if (['/recording', '/source', '/log'].includes(page)) tab = page;
  else if (SETTINGS_PAGES.includes(page)) tab = '/';

  return (
    <Box
      component="header"
      sx={{
        bgcolor: 'background.paper',
        borderBottom: 1,
        borderColor: 'divider',
      }}
    >
      <Box
        sx={{
          display: 'flex',
          flexWrap: 'wrap',
          alignItems: 'center',
          gap: '12px 24px',
          px: 2.5,
          py: 1.5,
        }}
      >
        <Stack
          direction="row"
          alignItems="center"
          spacing={1.5}
          sx={{ flex: '1 1 220px' }}
        >
          <img src={logo} alt="" width="36" height="36" />
          <Box>
            <Typography
              component="h1"
              sx={{ fontWeight: 600, fontSize: 16, lineHeight: 1.3 }}
            >
              CrewTimer Video Recorder
            </Typography>
            <Typography variant="caption" color="text.secondary">
              {`v${window.platform.appVersion}${waypoint ? ` · ${waypoint} waypoint` : ''}`}
            </Typography>
          </Box>
        </Stack>
        <Box
          sx={{ flex: '0 1 auto', display: 'flex', justifyContent: 'center' }}
        >
          <RecordingStatus />
        </Box>
        <Stack
          direction="row"
          alignItems="center"
          spacing={1}
          sx={{ flex: '1 1 auto', justifyContent: 'flex-end' }}
        >
          {updateAvailable && (
            <Button
              variant="outlined"
              size="small"
              onClick={() =>
                setDialogConfig({
                  title: 'Software Update Available',
                  body: (
                    <Stack>
                      <Typography>
                        Version: {latestVersion}: {latestText}.
                      </Typography>
                      <Link
                        href="https://github.com/crewtimer/crewtimer-recorder/releases/latest"
                        target="_blank"
                      >
                        Download from github
                      </Link>
                    </Stack>
                  ),
                  button: 'OK',
                  showCancel: false,
                })
              }
            >
              Update available
            </Button>
          )}
          {reportAllGaps && (
            <Tooltip title="All recording gaps reported as errors">
              <Visibility fontSize="small" color="action" />
            </Tooltip>
          )}
          <StartButton />
          <HamburgerMenu />
        </Stack>
      </Box>
      <Tabs
        value={tab}
        onChange={(_, value: string) => setSelectedPage(value)}
        sx={{ px: 1.5 }}
      >
        <Tab value="/" label="Camera" />
        <Tab value="/recording" label="Recording" />
        <Tab value="/source" label="Source" />
        <Tab value="/log" label="Event log" />
      </Tabs>
    </Box>
  );
}
