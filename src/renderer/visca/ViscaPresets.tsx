import React, { useState, MouseEvent } from 'react';
import {
  Menu,
  MenuItem,
  Box,
  ToggleButton,
  ToggleButtonGroup,
} from '@mui/material';
import { useCameraPresets, useCameraState } from './ViscaState';
import { updateCameraState } from './ViscaAPI';
import { setToast } from '../components/Toast';

const ViscaPresets: React.FC = () => {
  const [cameraState, setCameraState] = useCameraState();
  const [cameraPresets, setCameraPresets] = useCameraPresets();

  // Tracks whether "load" or "save" is currently selected (or null if none).
  const [mode, setMode] = useState<string | null>(null);

  // Holds the element that anchors the Menu (null = menu closed).
  const [anchorEl, setAnchorEl] = useState<HTMLElement | null>(null);

  // Handle toggling between Load and Save
  const handleModeChange = (
    event: MouseEvent<HTMLElement>,
    newMode: string | null,
  ) => {
    if (newMode) {
      // If a mode is selected, open the menu anchored to this button.
      setMode(newMode);
      setAnchorEl(event.currentTarget);
    } else {
      // If toggled off (no mode selected), close the menu.
      setMode(null);
      setAnchorEl(null);
    }
  };

  // Close the menu and reset mode
  const handleMenuClose = () => {
    setAnchorEl(null);
    setMode(null);
  };

  // User selects a preset from the menu
  const handleSelectPreset = (presetNumber: number) => {
    if (mode === 'load') {
      const newPreset = cameraPresets[presetNumber];
      if (newPreset) {
        setCameraState(newPreset);
        updateCameraState(newPreset);
        setToast({ severity: 'info', msg: `Preset ${presetNumber} loaded` });
      }
    } else if (mode === 'save') {
      setCameraPresets((prev) => {
        const newPresets = [...prev];
        newPresets[presetNumber] = cameraState;
        setCameraPresets(newPresets);
        return newPresets;
      });
      setToast({ severity: 'info', msg: `Preset ${presetNumber} saved` });
    }
    // Close menu and reset toggle
    handleMenuClose();
  };

  return (
    <Box display="flex" gap={2}>
      {/* Toggle Button Group for Load/Save */}
      <ToggleButtonGroup
        value={mode}
        exclusive
        onChange={handleModeChange}
        size="small"
        sx={{
          '& .MuiToggleButton-root': {
            padding: '2px 6px',
            minWidth: 0,
            borderRadius: '4px',
          },
        }}
      >
        <ToggleButton value="load" color="primary">
          Load
        </ToggleButton>
        <ToggleButton value="save" color="primary">
          Save
        </ToggleButton>
      </ToggleButtonGroup>

      {/* Menu pops up when user selects Load or Save */}
      <Menu
        anchorEl={anchorEl}
        open={Boolean(anchorEl)}
        onClose={handleMenuClose}
      >
        {[1, 2, 3, 4].map((num) => (
          <MenuItem
            key={num}
            disabled={mode === 'load' && cameraPresets[num] === undefined}
            onClick={() => handleSelectPreset(num)}
          >
            {num}
          </MenuItem>
        ))}
      </Menu>
    </Box>
  );
};

export default ViscaPresets;
