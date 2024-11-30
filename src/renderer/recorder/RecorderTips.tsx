import EditIcon from '@mui/icons-material/Edit';
import CheckIcon from '@mui/icons-material/Check';
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
        <EditIcon />
        <Typography>Edit finish line and crop region</Typography>
      </Box>
      <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
        <CheckIcon />
        <Typography>Save edit changes</Typography>
      </Box>
      <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
        <FullscreenIcon />
        <Typography>Set crop to fullscreen</Typography>
      </Box>
      <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
        <VerticalAlignCenterIcon sx={{ transform: 'rotate(90deg)' }} />
        <Typography>Set finish to center</Typography>
      </Box>
    </Box>
  );
};

export default RecorderTips;