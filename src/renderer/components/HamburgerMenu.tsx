import { useState } from 'react';
import * as React from 'react';
import MenuIcon from '@mui/icons-material/Menu';
import InfoIcon from '@mui/icons-material/Info';
import HelpIcon from '@mui/icons-material/Help';
import CameraIcon from '@mui/icons-material/Camera';
import SecurityIcon from '@mui/icons-material/Security';
import Visibility from '@mui/icons-material/Visibility';
import VisibilityOff from '@mui/icons-material/VisibilityOff';
import LightModeIcon from '@mui/icons-material/LightMode';
import DarkModeIcon from '@mui/icons-material/DarkMode';
import SettingsBrightnessIcon from '@mui/icons-material/SettingsBrightness';
import QueryStatsIcon from '@mui/icons-material/QueryStats';
import Menu from '@mui/material/Menu';
import MenuItem from '@mui/material/MenuItem';
import { Divider, IconButton, ListItemIcon, ListItemText } from '@mui/material';
import { useSelectedPage } from '../pages/SelectedPage';
import { setToast } from './Toast';
import { useViscaIP } from '../visca/ViscaState';
import {
  useNerdMode,
  useRecordingPropsPending,
  useReportAllGaps,
} from '../recorder/RecorderData';
import { openNagScreen } from './NagScreen';
import { UseStoredDatum } from '../store/UseElectronDatum';

const AboutText = `CrewTimer Video Recorder ${window.platform.appVersion}`;

type ThemeMode = 'system' | 'light' | 'dark';
const [useThemeMode] = UseStoredDatum<ThemeMode>('themeMode', 'system');

const nextThemeMode: Record<ThemeMode, ThemeMode> = {
  system: 'light',
  light: 'dark',
  dark: 'system',
};
const themeModeIcon = {
  system: <SettingsBrightnessIcon />,
  light: <LightModeIcon />,
  dark: <DarkModeIcon />,
};

const HamburgerMenu = () => {
  const [, setSelectedPage] = useSelectedPage();
  const [anchorEl, setAnchorEl] = useState<null | HTMLElement>(null);
  const [reportAllGaps, setReportAllGaps] = useReportAllGaps();
  const [, setRecordingPropsPending] = useRecordingPropsPending();
  const [viscaIP] = useViscaIP();
  const [themeMode, setThemeMode] = useThemeMode();
  const [nerdMode, setNerdMode] = useNerdMode();
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
          <MenuItem
            onClick={() => {
              handleClose();
              window.open(`http://${viscaIP}`, '_blank');
            }}
          >
            <ListItemIcon>
              <CameraIcon />
            </ListItemIcon>
            <ListItemText>Camera web page</ListItemText>
          </MenuItem>
        )}
        <MenuItem
          onClick={() => {
            handleClose();
            openNagScreen(true);
          }}
        >
          <ListItemIcon>
            <InfoIcon />
          </ListItemIcon>
          <ListItemText primary="What's New" />
        </MenuItem>
        {/* Stays open so the user can cycle through modes and see each one */}
        <MenuItem onClick={() => setThemeMode(nextThemeMode[themeMode])}>
          <ListItemIcon>{themeModeIcon[themeMode]}</ListItemIcon>
          <ListItemText
            primary={`Theme: ${themeMode[0].toUpperCase()}${themeMode.slice(1)}`}
          />
        </MenuItem>
        <MenuItem
          onClick={() => {
            handleClose();
            setNerdMode(!nerdMode);
          }}
        >
          <ListItemIcon>
            <QueryStatsIcon />
          </ListItemIcon>
          <ListItemText primary={`Nerd mode: ${nerdMode ? 'On' : 'Off'}`} />
        </MenuItem>
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
      </Menu>
    </div>
  );
};

export default HamburgerMenu;
