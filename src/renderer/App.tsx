import { useEffect, useMemo } from 'react';
import {
  ThemeProvider,
  StyledEngineProvider,
  createTheme,
} from '@mui/material/styles';
import Box from '@mui/material/Box';
import useMediaQuery from '@mui/material/useMediaQuery';
import CssBaseline from '@mui/material/CssBaseline';
import MainPage from './pages/MainPage';
import { TopBar } from './components/TopBar';
import { ConfirmDialog } from './components/ConfirmDialog';
import Toast from './components/Toast';
import { CameraMonitor } from './recorder/CameraMonitor';
import { openNagScreen } from './components/NagScreen';
import { StatusBar } from './components/StatusBar';

const palettes = {
  light: {
    mode: 'light',
    primary: { main: '#1F5FAD' },
    error: { main: '#C9252C' },
    warning: { main: '#9A5B00' },
    success: { main: '#1A7F4B' },
    divider: '#D5DCE3',
    background: { default: '#EEF1F4', paper: '#FFFFFF' },
  },
  dark: {
    mode: 'dark',
    primary: { main: '#7DB4F5' },
    error: { main: '#FF6B70' },
    warning: { main: '#F5A524' },
    success: { main: '#3DD68C' },
    divider: '#262E37',
    background: { default: '#0E1114', paper: '#151A1F' },
  },
} as const;

function App() {
  // The main process maps the Theme menu choice onto prefers-color-scheme
  const mode = useMediaQuery('(prefers-color-scheme: dark)') ? 'dark' : 'light';
  const theme = useMemo(
    () =>
      createTheme({
        palette: palettes[mode],
        shape: { borderRadius: 8 },
        typography: {
          fontFamily: '"Segoe UI", system-ui, -apple-system, sans-serif',
          button: { textTransform: 'none', fontWeight: 600 },
        },
      }),
    [mode],
  );

  useEffect(() => {
    setTimeout(() => {
      openNagScreen();
    }, 200);
  }, []);
  return (
    <StyledEngineProvider injectFirst>
      <ThemeProvider theme={theme}>
        <CssBaseline />
        <ConfirmDialog />
        <Toast />
        <CameraMonitor />
        <Box sx={{ display: 'flex', flexDirection: 'column', height: '100vh' }}>
          <TopBar />
          <Box
            component="main"
            sx={{ flex: 1, minHeight: 0, overflow: 'auto', p: 2.5 }}
          >
            <MainPage />
          </Box>
          <StatusBar />
        </Box>
      </ThemeProvider>
    </StyledEngineProvider>
  );
}

export default App;
