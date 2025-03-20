import CropIcon from '@mui/icons-material/Crop';
import FullscreenIcon from '@mui/icons-material/Fullscreen';
import VerticalAlignCenterIcon from '@mui/icons-material/VerticalAlignCenter';
import MouseIcon from '@mui/icons-material/Mouse';
import { Box, Typography } from '@mui/material';

const RecorderTips = () => {
  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
      <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
        <MouseIcon />
        <Typography>Right click to zoom</Typography>
      </Box>
      <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
        <CropIcon />
        <Typography>Define crop region</Typography>
      </Box>
      <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
        <FullscreenIcon />
        <Typography>Set crop to fullscreen</Typography>
      </Box>
      <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
        <VerticalAlignCenterIcon sx={{ transform: 'rotate(90deg)' }} />
        <Typography>Set finish line to center</Typography>
      </Box>
      <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
        <Typography>
          Shift to drag only top or bottom finish position
        </Typography>
      </Box>
    </Box>
  );
};

export default RecorderTips;
