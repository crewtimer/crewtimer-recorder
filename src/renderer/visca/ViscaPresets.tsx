import React, { useState, MouseEvent } from 'react';
import {
  Menu,
  MenuItem,
  Box,
  ToggleButton,
  ToggleButtonGroup,
  CircularProgress,
} from '@mui/material';
import {
  CameraPreset,
  CameraState,
  useCameraPresets,
  useCameraState,
} from './ViscaState';
import { getCameraState, updateCameraState } from './ViscaAPI';
import { setToast } from '../components/Toast';
import { showErrorDialog } from '../components/ErrorDialog';
import { snooze } from '../util/Util';
import {
  getGuide,
  getIsRecording,
  getRecordingProps,
  setGuide,
  useRecordingProps,
} from '../recorder/RecorderData';
import { startRecording, stopRecording } from '../recorder/RecorderApi';
import { Rect } from '../recorder/RecorderTypes';

const sameRect = (a: Rect, b: Rect) =>
  a.x === b.x && a.y === b.y && a.width === b.width && a.height === b.height;

const reportPresetMismatches = async (
  presetNumber: number,
  preset: CameraState,
) => {
  await snooze(1000);
  const actualState = await getCameraState();

  (Object.keys(preset) as (keyof typeof preset)[]).forEach((setting) => {
    if (actualState[setting] !== preset[setting]) {
      console.warn(
        `Preset ${presetNumber} mismatch: ${setting} expected=${preset[setting]}, actual=${actualState[setting]}`,
      );
    }
  });
};

const ViscaPresets: React.FC = () => {
  const [, setCameraState] = useCameraState();
  const [cameraPresets, setCameraPresets] = useCameraPresets();
  const [, setRecordingProps] = useRecordingProps();
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

  // Restore a preset's crop region and timing line, restarting a recording so the recorder picks up both
  const applyPresetCrop = ({ cropArea, guide }: CameraPreset) => {
    const priorGuide = getGuide();
    const guideChanged =
      !!guide && (guide.pt1 !== priorGuide.pt1 || guide.pt2 !== priorGuide.pt2);
    const cropChanged =
      !!cropArea && !sameRect(cropArea, getRecordingProps().cropArea);
    if (guideChanged) {
      setGuide(guide);
    }
    if (cropChanged) {
      setRecordingProps((prior) => ({ ...prior, cropArea }));
    }
    if ((guideChanged || cropChanged) && getIsRecording()) {
      stopRecording().catch(showErrorDialog);
      setTimeout(() => startRecording().catch(showErrorDialog), 100);
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
          const camState = { ...newPreset };
          delete camState.cropArea;
          delete camState.guide;
          applyPresetCrop(newPreset);
          setCameraState(camState); // in-memory settings
          await updateCameraState(camState); // send to cameras
          await reportPresetMismatches(presetNumber, camState);
          setToast({ severity: 'info', msg: `Preset ${presetNumber} loaded` });
          console.log(`Load: ${JSON.stringify(newPreset)}`);
        }
      } else if (mode === 'save') {
        const camState = await getCameraState(); // read from camera
        const newPresets = [...cameraPresets];
        newPresets[presetNumber] = {
          ...camState,
          cropArea: { ...getRecordingProps().cropArea },
          guide: { ...getGuide() },
        };
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
    <Box display="flex" alignItems="center" gap={1}>
      {/* Toggle Button Group for Load/Save */}
      <ToggleButtonGroup
        value={mode}
        exclusive
        onChange={handleModeChange}
        size="small"
        color="primary"
        aria-label="Camera presets"
        sx={{ height: 36 }}
      >
        <ToggleButton value="load">Load</ToggleButton>
        <ToggleButton value="save">Save</ToggleButton>
      </ToggleButtonGroup>
      {showProgress && <CircularProgress size={20} />}

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
