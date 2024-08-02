import {
  Toolbar,
  IconButton,
  Typography,
  Box,
  Button,
  Stack,
  Link,
} from '@mui/material';
import { UseDatum } from 'react-usedatum';
import MenuIcon from '@mui/icons-material/Menu';
import logo from '../../../assets/icons/crewtimer.svg';
import HamburgerMenu from './HamburgerMenu';
import RecordingStatus from '../recorder/RecordingStatus';
import { useFirebaseDatum } from '../util/UseFirebase';
import { setDialogConfig } from './ConfirmDialog';

export const drawerWidth = 240;
export const [useDrawerOpen] = UseDatum(false);

const versionAsNumber = (version: string) => {
  const parts = version.split('.');
  return Number(parts[0]) * 100 + Number(parts[1]) * 10 + Number(parts[2]);
};

export function TopBar() {
  const [open, setOpen] = useDrawerOpen();
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

  return (
    <Toolbar
      sx={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'flex-start',
      }}
    >
      <IconButton
        edge="start"
        color="inherit"
        aria-label="open drawer"
        onClick={() => setOpen(true)}
        sx={{ marginRight: 2, display: open ? 'none' : 'inherit' }}
        size="large"
      >
        <MenuIcon />
      </IconButton>
      <img
        src={logo}
        alt="CrewTimer"
        width="40"
        height="40"
        style={{ marginLeft: 8, marginRight: 8 }}
      />
      <Typography component="h1" variant="h6" color="inherit" noWrap>
        CrewTimer Video Recorder
      </Typography>
      <Box sx={{ flexGrow: 1 }} />
      {updateAvailable && (
        <Button
          variant="outlined"
          color="inherit"
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
                    href="https://crewtimer.com/help/downloads"
                    target="_blank"
                  >
                    https://crewtimer.com/help/downloads
                  </Link>
                </Stack>
              ),
              button: 'OK',
              showCancel: false,
            })
          }
        >
          Update
        </Button>
      )}
      <Box sx={{ alignItems: 'center' }}>
        <RecordingStatus />
      </Box>
      <Box
        sx={{
          width: 48 + 8 + 8,
          display: 'flex',
          justifyContent: 'flex-end', // Aligns the HamburgerMenu to the right
          alignItems: 'center',
        }}
      >
        <HamburgerMenu />
      </Box>
    </Toolbar>
  );
}
