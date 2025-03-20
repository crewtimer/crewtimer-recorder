import React, { useState } from 'react';
import {
  TextField,
  IconButton,
  Menu,
  MenuItem,
  FormControl,
  Tooltip,
} from '@mui/material';
import MoreVertIcon from '@mui/icons-material/MoreVert';
import { useViscaPort } from './ViscaState';

const knownPorts = [
  { port: 52381, vendor: 'AIDA' },
  { port: 5678, vendor: 'SMTAV' },
  { port: 1259, vendor: 'PtzOptics' },
];

export const ViscaPortSelector: React.FC = () => {
  const [menuAnchorEl, setMenuAnchorEl] = useState<null | HTMLElement>(null);
  const [viscaPort, setViscaPort] = useViscaPort();

  // Used to open the pop-up menu of known ports
  const handleOpenMenu = (event: React.MouseEvent<HTMLElement>) => {
    setMenuAnchorEl(event.currentTarget);
  };

  // Closes the pop-up menu
  const handleCloseMenu = () => {
    setMenuAnchorEl(null);
  };

  // When a known port is selected from the menu
  const handleSelectKnownPort = (selectedPort: number) => {
    setViscaPort(selectedPort);
    handleCloseMenu();
  };

  // When user types in the text field
  const handleChangePort = (event: React.ChangeEvent<HTMLInputElement>) => {
    const newPort = parseInt(event.target.value, 10);
    // If parse fails, newPort is NaN => fallback to 0 or last valid
    const validPort = Number.isNaN(newPort) ? 0 : newPort;
    setViscaPort(validPort);
  };

  return (
    <FormControl size="small">
      <div style={{ display: 'flex', alignItems: 'center' }}>
        <Tooltip title="Select Visca control port to control camera exposure, zoom, and focus. Click the menu button for common presets.">
          <TextField
            label="Visca Port"
            type="number"
            value={viscaPort}
            onChange={handleChangePort}
            size="small"
            sx={{ width: '8rem' }} // Make the text field narrower
          />
        </Tooltip>
        <IconButton onClick={handleOpenMenu} style={{ marginLeft: 8 }}>
          <MoreVertIcon />
        </IconButton>
      </div>
      <Menu
        anchorEl={menuAnchorEl}
        open={Boolean(menuAnchorEl)}
        onClose={handleCloseMenu}
      >
        {knownPorts.map((p) => (
          <MenuItem
            key={p.vendor}
            onClick={() => handleSelectKnownPort(p.port)}
            value={p.port}
          >
            {`${p.port} ${p.vendor}`}
          </MenuItem>
        ))}
      </Menu>
    </FormControl>
  );
};
