import React, { useState, MouseEvent } from 'react';
import {
  Menu,
  MenuItem,
  Box,
  ToggleButton,
  ToggleButtonGroup,
  CircularProgress,
} from '@mui/material';
import { useCameraPresets, useCameraState } from './ViscaState';
import { getCameraState, updateCameraState } from './ViscaAPI';
import { setToast } from '../components/Toast';

const ViscaPresets: React.FC = () => {
  const [, setCameraState] = useCameraState();
  const [cameraPresets, setCameraPresets] = useCameraPresets();
  const [showProgress, setshowProgress] = useState(false);

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
  const handleSelectPreset = async (presetNumber: number) => {
    setshowProgress(true);
    try {
      if (mode === 'load') {
        const newPreset = cameraPresets[presetNumber];
        if (newPreset) {
          setCameraState(newPreset); // in-memory settings
          await updateCameraState(newPreset); // send to cameras
          setToast({ severity: 'info', msg: `Preset ${presetNumber} loaded` });
          console.log(`Load: ${JSON.stringify(newPreset)}`);
        }
      } else if (mode === 'save') {
        const camState = await getCameraState(); // read from camera
        const newPresets = [...cameraPresets];
        newPresets[presetNumber] = camState;
        console.log(`Save: ${JSON.stringify(camState)}`);
        setCameraPresets(newPresets); // save to disk

        setToast({ severity: 'info', msg: `Preset ${presetNumber} saved` });
      }
    } catch {
      /* ignore */
    }
    // Close menu and reset toggle

    setshowProgress(false);
    handleMenuClose();
  };

  return (
    <Box display="flex" gap={2}>
      {showProgress && <CircularProgress />}
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
        {[0, 1, 2, 3, 4].map((num) => (
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
