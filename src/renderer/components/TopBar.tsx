import { Toolbar, IconButton, Typography, Box } from '@mui/material';
import { UseDatum } from 'react-usedatum';
import MenuIcon from '@mui/icons-material/Menu';
import logo from '../../../assets/icons/crewtimer.svg';
import HamburgerMenu from './HamburgerMenu';

export const drawerWidth = 240;
export const [useDrawerOpen] = UseDatum(false);

export function TopBar() {
  const [open, setOpen] = useDrawerOpen();

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
      <Typography
        component="h1"
        variant="h6"
        color="inherit"
        noWrap
        sx={{ flexGrow: 1, textAlign: 'center' }}
      >
        CrewTimer Video Recorder
      </Typography>
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
