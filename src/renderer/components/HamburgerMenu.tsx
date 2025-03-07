import { useState } from 'react';
import * as React from 'react';
import MenuIcon from '@mui/icons-material/Menu';
import InfoIcon from '@mui/icons-material/Info';
import HelpIcon from '@mui/icons-material/Help';
import SecurityIcon from '@mui/icons-material/Security';
import Menu from '@mui/material/Menu';
import MenuItem from '@mui/material/MenuItem';
import { IconButton, ListItemIcon, ListItemText } from '@mui/material';
import { useSelectedPage } from '../pages/MainPage';
import { setToast } from './Toast';
import { sendViscaCommandToDevice } from '../recorder/RecorderApi';

const AboutText = `CrewTimer Video Recorder ${window.platform.appVersion}`;

const HamburgerMenu = () => {
  const [, setSelectedPage] = useSelectedPage();
  const [anchorEl, setAnchorEl] = useState<null | HTMLElement>(null);
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

  const testVisca = () => {
    sendViscaCommandToDevice({
      ip: '10.0.1.188',
      port: 52381,
      data: Uint8Array.from([1, 2, 3, 255]),
    })
      .then((result) => console.log(`got ${JSON.stringify(result)}`))
      .catch((reason) => console.log(`got err ${reason}`));
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
        <MenuItem onClick={closeAndGo('/privacy')}>
          <ListItemIcon>
            <SecurityIcon />
          </ListItemIcon>
          <ListItemText>Privacy Policy</ListItemText>
        </MenuItem>
        <MenuItem onClick={testVisca}>
          <ListItemIcon>
            <SecurityIcon />
          </ListItemIcon>
          <ListItemText>Test Visca</ListItemText>
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
