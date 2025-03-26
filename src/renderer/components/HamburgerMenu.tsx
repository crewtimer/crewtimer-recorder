import { useState } from 'react';
import * as React from 'react';
import MenuIcon from '@mui/icons-material/Menu';
import InfoIcon from '@mui/icons-material/Info';
import HelpIcon from '@mui/icons-material/Help';
import CameraIcon from '@mui/icons-material/Camera';
import SecurityIcon from '@mui/icons-material/Security';
import Menu from '@mui/material/Menu';
import MenuItem from '@mui/material/MenuItem';
import { IconButton, ListItemIcon, ListItemText } from '@mui/material';
import { useSelectedPage } from '../pages/MainPage';
import { setToast } from './Toast';
import { useViscaIP } from '../visca/ViscaState';

const AboutText = `CrewTimer Video Recorder ${window.platform.appVersion}`;

const HamburgerMenu = () => {
  const [, setSelectedPage] = useSelectedPage();
  const [anchorEl, setAnchorEl] = useState<null | HTMLElement>(null);
  const [viscaIP] = useViscaIP();
  const open = Boolean(anchorEl);
  const handleClick = (event: React.MouseEvent<HTMLButtonElement>) => {
    setAnchorEl(event.currentTarget);
  };
  const handleClose = () => {
    setAnchorEl(null);
  };

  const closeAndGo = (path: string) => () => {
    setAnchorEl(null);
    setSelectedPage(path);
  };

  return (
    <div>
      <IconButton
        key="menu"
        color="inherit"
        aria-label="Menu"
        onClick={handleClick}
        size="large"
      >
        <MenuIcon />
      </IconButton>
      <Menu
        id="basic-menu"
        anchorEl={anchorEl}
        open={open}
        onClose={handleClose}
        MenuListProps={{
          'aria-labelledby': 'basic-button',
        }}
      >
        <MenuItem onClick={closeAndGo('/help')}>
          {' '}
          <ListItemIcon>
            <HelpIcon />
          </ListItemIcon>
          <ListItemText>Help</ListItemText>
        </MenuItem>
        {viscaIP && (
          <MenuItem onClick={() => window.open(`http://${viscaIP}`, '_blank')}>
            <ListItemIcon>
              <CameraIcon />
            </ListItemIcon>
            <ListItemText>Camera web page</ListItemText>
          </MenuItem>
        )}
        <MenuItem onClick={closeAndGo('/privacy')}>
          <ListItemIcon>
            <SecurityIcon />
          </ListItemIcon>
          <ListItemText>Privacy Policy</ListItemText>
        </MenuItem>
        <MenuItem
          onClick={() => {
            handleClose();
            setToast({ severity: 'info', msg: AboutText });
          }}
        >
          <ListItemIcon>
            <InfoIcon />
          </ListItemIcon>
          <ListItemText primary="About" />
        </MenuItem>
      </Menu>
    </div>
  );
};

export default HamburgerMenu;
