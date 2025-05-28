import { useState } from 'react';
import * as React from 'react';
import MenuIcon from '@mui/icons-material/Menu';
import InfoIcon from '@mui/icons-material/Info';
import HelpIcon from '@mui/icons-material/Help';
import CameraIcon from '@mui/icons-material/Camera';
import SecurityIcon from '@mui/icons-material/Security';
import Visibility from '@mui/icons-material/Visibility';
import VisibilityOff from '@mui/icons-material/VisibilityOff';
import AccessTime from '@mui/icons-material/AccessTime';
import AccessTimeTwoToneIcon from '@mui/icons-material/AccessTimeTwoTone';
import Menu from '@mui/material/Menu';
import MenuItem from '@mui/material/MenuItem';
import { Divider, IconButton, ListItemIcon, ListItemText } from '@mui/material';
import { useSelectedPage } from '../pages/MainPage';
import { setToast } from './Toast';
import { useViscaIP } from '../visca/ViscaState';
import {
  useAddTimeOverlay,
  useRecordingPropsPending,
  useReportAllGaps,
} from '../recorder/RecorderData';

const AboutText = `CrewTimer Video Recorder ${window.platform.appVersion}`;

const HamburgerMenu = () => {
  const [, setSelectedPage] = useSelectedPage();
  const [anchorEl, setAnchorEl] = useState<null | HTMLElement>(null);
  const [reportAllGaps, setReportAllGaps] = useReportAllGaps();
  const [addTimeOverlay, setAddTimeOverlay] = useAddTimeOverlay();
  const [, setRecordingPropsPending] = useRecordingPropsPending();
  const [viscaIP] = useViscaIP();
  const [shiftMenu, setShiftMenu] = useState(false);
  const open = Boolean(anchorEl);
  const handleClick = (event: React.MouseEvent<HTMLButtonElement>) => {
    setAnchorEl(event.currentTarget);
    setShiftMenu(event.shiftKey);
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
        {shiftMenu && <Divider />}
        {shiftMenu && (
          <MenuItem
            onClick={() =>
              setReportAllGaps((prior) => {
                setRecordingPropsPending(true);
                handleClose();
                return !prior;
              })
            }
          >
            <ListItemIcon>
              {reportAllGaps ? (
                <Visibility fontSize="small" />
              ) : (
                <VisibilityOff fontSize="small" />
              )}
            </ListItemIcon>
            <ListItemText primary="Report All Gaps" />
          </MenuItem>
        )}
        {shiftMenu && (
          <MenuItem
            onClick={() =>
              setAddTimeOverlay((prior) => {
                setRecordingPropsPending(true);
                handleClose();
                return !prior;
              })
            }
          >
            <ListItemIcon>
              {addTimeOverlay ? (
                <AccessTime fontSize="small" />
              ) : (
                <AccessTimeTwoToneIcon fontSize="small" />
              )}
            </ListItemIcon>
            <ListItemText primary="Add Time Overlay" />
          </MenuItem>
        )}
      </Menu>
    </div>
  );
};

export default HamburgerMenu;
