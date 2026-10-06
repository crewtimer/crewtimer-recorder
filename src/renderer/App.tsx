import { useEffect, useMemo } from 'react';
import {
  ThemeProvider,
  Theme,
  StyledEngineProvider,
  createTheme,
  styled,
} from '@mui/material/styles';
import Box from '@mui/material/Box';
import useMediaQuery from '@mui/material/useMediaQuery';
import MuiDrawer, { DrawerProps as MuiDrawerProps } from '@mui/material/Drawer';
import MuiAppBar, { AppBarProps as MuiAppBarProps } from '@mui/material/AppBar';
import CssBaseline from '@mui/material/CssBaseline';
import Divider from '@mui/material/Divider';
import IconButton from '@mui/material/IconButton';
import ChevronLeftIcon from '@mui/icons-material/ChevronLeft';
import ChevronRightIcon from '@mui/icons-material/ChevronRight';
import SideNavMenu from './components/SideNavMenu';
import MainPage from './pages/MainPage';
import { drawerWidth, TopBar, useDrawerOpen } from './components/TopBar';
import { ConfirmDialog } from './components/ConfirmDialog';
import Toast from './components/Toast';
import { CameraMonitor } from './recorder/CameraMonitor';
import { openNagScreen } from './components/NagScreen';

const palettes = {
  light: {
    mode: 'light',
    primary: { main: '#1F5FAD' },
    background: { default: '#EEF1F4', paper: '#FFFFFF' },
  },
  dark: {
    mode: 'dark',
    primary: { main: '#7DB4F5' },
    background: { default: '#0E1114', paper: '#151A1F' },
  },
} as const;

const openedMixin = (theme: Theme) => ({
  width: drawerWidth,
  transition: theme.transitions.create('width', {
    easing: theme.transitions.easing.sharp,
    duration: theme.transitions.duration.enteringScreen,
  }),
  overflowX: 'hidden',
});

const closedMixin = (theme: Theme) => ({
  transition: theme.transitions.create('width', {
    easing: theme.transitions.easing.sharp,
    duration: theme.transitions.duration.leavingScreen,
  }),
  overflowX: 'hidden',
  width: `calc(${theme.spacing(7)} + 1px)`,
  [theme.breakpoints.up('sm')]: {
    width: `calc(${theme.spacing(9)} + 1px)`,
  },
});

interface DrawerProps extends MuiDrawerProps {
  open: boolean;
}
const Drawer = styled(MuiDrawer, {
  shouldForwardProp: (prop) => prop !== 'open',
})<DrawerProps>(({ theme, open }) => ({
  flexShrink: 0,
  whiteSpace: 'nowrap',
  boxSizing: 'border-box',
  transition: theme.transitions.create('width', {
    easing: theme.transitions.easing.sharp,
    duration: open
      ? theme.transitions.duration.enteringScreen
      : theme.transitions.duration.leavingScreen,
  }),
  overflowX: 'hidden',
  ...(open
    ? { width: drawerWidth, '& .MuiDrawer-paper': openedMixin(theme) }
    : {
        width: `calc(${theme.spacing(7)} + 1px)`,
        [theme.breakpoints.up('sm')]: {
          width: `calc(${theme.spacing(9)} + 1px)`,
        },
        '& .MuiDrawer-paper': closedMixin(theme),
      }),
}));

const DrawerHeader = styled('div')(({ theme }) => ({
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'flex-end',
  padding: theme.spacing(0, 1),
  // necessary for content to be below app bar
  ...theme.mixins.toolbar,
}));
interface AppBarProps extends MuiAppBarProps {
  open?: boolean;
}
const AppBar = styled(MuiAppBar, {
  shouldForwardProp: (prop) => prop !== 'open',
})<AppBarProps>(({ theme, open }) => ({
  zIndex: theme.zIndex.drawer + 1,
  transition: theme.transitions.create(['width', 'margin'], {
    easing: theme.transitions.easing.sharp,
    duration: theme.transitions.duration.leavingScreen,
  }),
  ...(open && {
    marginLeft: drawerWidth,
    width: `calc(100% - ${drawerWidth}px)`,
    transition: theme.transitions.create(['width', 'margin'], {
      easing: theme.transitions.easing.sharp,
      duration: theme.transitions.duration.enteringScreen,
    }),
  }),
}));

function App() {
  const [open, setOpen] = useDrawerOpen();
  // The main process maps the Theme menu choice onto prefers-color-scheme
  const mode = useMediaQuery('(prefers-color-scheme: dark)') ? 'dark' : 'light';
  const theme = useMemo(() => createTheme({ palette: palettes[mode] }), [mode]);

  useEffect(() => {
    setTimeout(() => {
      openNagScreen();
    }, 200);
  }, []);
  return (
    <StyledEngineProvider injectFirst>
      <ThemeProvider theme={theme}>
        <Box sx={{ display: 'flex' }}>
          <CssBaseline />
          <ConfirmDialog />
          <Toast />
          <CameraMonitor />
          <AppBar position="fixed" open={open}>
            <TopBar />
          </AppBar>
          <Drawer variant="permanent" open={open}>
            <DrawerHeader>
              <IconButton onClick={() => setOpen(false)}>
                {theme.direction === 'rtl' ? (
                  <ChevronRightIcon />
                ) : (
                  <ChevronLeftIcon />
                )}
              </IconButton>
            </DrawerHeader>
            <Divider />
            <SideNavMenu />
          </Drawer>
          <Box
            component="main"
            sx={{
              flexGrow: 1,
              p: 3,
              display: 'flex',
              height: '100vh',
              flexDirection: 'column',
              width: '100%',
            }}
          >
            <DrawerHeader />
            <MainPage />
          </Box>
        </Box>
      </ThemeProvider>
    </StyledEngineProvider>
  );
}

export default App;
