import { useState } from 'react';
import { useNavigate } from 'react-router-dom';

import * as React from 'react';
import MenuIcon from '@mui/icons-material/Menu';
import Menu from '@mui/material/Menu';
import MenuItem from '@mui/material/MenuItem';
import { IconButton } from '@mui/material';

const HamburgerMenu = () => {
  const navigate = useNavigate();
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
    navigate(path);
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
        <MenuItem onClick={closeAndGo('/help')}>Help</MenuItem>
        <MenuItem onClick={closeAndGo('/privacy')}>Privacy Policy</MenuItem>
      </Menu>
    </div>
  );
};

export default HamburgerMenu;
