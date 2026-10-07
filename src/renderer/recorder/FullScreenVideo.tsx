import { Box } from '@mui/material';
import { FullSizeWindow } from '../components/FullSizeWindow';
import PreviewCanvas from '../components/PreviewCanvas';
import ViscaControlPanel from '../visca/ViscaControlPanel';

/** Camera control tab: same layout as the Recorder tab, preview left and controls right. */
export const FullScreenVideo = () => (
  <Box
    sx={{
      display: 'flex',
      flexWrap: { xs: 'wrap', md: 'nowrap' },
      gap: 2.5,
      height: '100%',
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
      <FullSizeWindow component={PreviewCanvas} />
    </Box>
    <Box
      component="aside"
      aria-label="Camera controls"
      sx={{ flex: '1 1 320px', maxWidth: 400, overflowY: { md: 'auto' } }}
    >
      <ViscaControlPanel />
    </Box>
  </Box>
);
