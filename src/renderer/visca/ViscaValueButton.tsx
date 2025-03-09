import React, { useEffect, useState } from 'react';
import {
  IconButton,
  Switch,
  Grid,
  Typography,
  ToggleButtonGroup,
  ToggleButton,
} from '@mui/material';
import RemoveIcon from '@mui/icons-material/Remove';
import AddIcon from '@mui/icons-material/Add';
import { sendViscaCommand, ViscaCommand } from './ViscaAPI';
import { setToast } from '../components/Toast';

interface ViscaValueButtonProps {
  title: string;
  decrement: ViscaCommand;
  increment: ViscaCommand;
  reset: ViscaCommand;
  value?: boolean;
  autoOn?: ViscaCommand;
  autoOff?: ViscaCommand;
  autoOnce?: ViscaCommand;
}
const ViscaValueButton: React.FC<ViscaValueButtonProps> = ({
  title,
  decrement,
  increment,
  reset,
  value,
  autoOn,
  autoOff,
  autoOnce,
}) => {
  const [isAuto, setIsAuto] = useState<boolean>(value === true);
  useEffect(() => {
    setIsAuto(value === true);
  }, [value]);

  const handleCommand = async (command: ViscaCommand | undefined) => {
    try {
      if (command === undefined) {
        return;
      }
      await sendViscaCommand(command);
      // if (result) {
      //   const state = await getCameraState();
      //   if (state) {
      //     setCameraState(state);
      //   }
      // }
    } catch (error) {
      setToast({
        severity: 'error',
        msg: `Error sending VISCA command: ${error}`,
      });
      console.error('Error sending VISCA command:', error);
    }
  };

  // Decrement press & release
  const handleDecrementPress = () => {
    setIsAuto(false); // Turn off auto
    handleCommand(decrement);
  };

  const handleDecrementRelease = () => {
    handleCommand(reset);
  };

  // Increment press & release
  const handleIncrementPress = () => {
    setIsAuto(false); // Turn off auto
    handleCommand(increment);
  };

  const handleIncrementRelease = () => {
    handleCommand(reset);
  };

  const handleToggleMode = (
    _event: React.MouseEvent<HTMLElement>,
    newValue: 'auto' | 'man' | 'push' | null,
  ) => {
    // If user deselects all, newValue will be null; we ignore that
    switch (newValue) {
      case 'auto':
        handleCommand(autoOn);
        setIsAuto(true);
        break;
      case 'man':
        handleCommand(autoOff);
        setIsAuto(false);
        break;
      case 'push':
        handleCommand(autoOnce);
        break;
      default:
    }
  };

  return (
    <Grid
      container
      alignItems="center"
      justifyContent="flex-start"
      spacing={1}
      sx={{ width: 'fit-content' }}
    >
      {/* Title (fixed width to align multiple panels) */}
      {title && (
        <Grid item sx={{ width: '65px' }}>
          <Typography variant="subtitle1" noWrap>
            {title}
          </Typography>
        </Grid>
      )}
      {/* Decrement Button */}
      <Grid item>
        <IconButton
          onMouseDown={handleDecrementPress}
          onMouseUp={handleDecrementRelease}
          onTouchStart={handleDecrementPress}
          onTouchEnd={handleDecrementRelease}
          size="small"
          sx={{
            // Slightly smaller button
            padding: '4px',
            // Light background & thin border
            backgroundColor: 'rgba(0, 0, 0, 0.04)',
            border: '1px solid rgba(0,0,0,0.23)',
            // Darker background on press
            '&:active': {
              backgroundColor: 'rgba(0, 0, 0, 0.20)',
            },
          }}
        >
          <RemoveIcon fontSize="small" />
        </IconButton>
      </Grid>
      {/* Increment Button */}
      <Grid item>
        <IconButton
          onMouseDown={handleIncrementPress}
          onMouseUp={handleIncrementRelease}
          onTouchStart={handleIncrementPress}
          onTouchEnd={handleIncrementRelease}
          size="small"
          sx={{
            // Slightly smaller button
            padding: '4px',
            // Light background & thin border
            backgroundColor: 'rgba(0, 0, 0, 0.04)',
            border: '1px solid rgba(0,0,0,0.23)',
            // Darker background on press
            '&:active': {
              backgroundColor: 'rgba(0, 0, 0, 0.20)',
            },
          }}
        >
          <AddIcon fontSize="small" />
        </IconButton>
      </Grid>
      {autoOn && (
        <Grid item>
          <ToggleButtonGroup
            exclusive
            value={isAuto ? 'auto' : 'man'}
            onChange={handleToggleMode}
            size="small"
            sx={{
              '& .MuiToggleButton-root': {
                padding: '2px 6px', // tighter padding for each toggle button
                minWidth: 0, // prevents fixed wide buttons
                borderRadius: '4px',
              },
            }}
          >
            <ToggleButton value="auto">Auto</ToggleButton>
            <ToggleButton value="man">Man</ToggleButton>
            <ToggleButton value="push">Push</ToggleButton>
          </ToggleButtonGroup>
        </Grid>
      )}
    </Grid>
  );
};

export default ViscaValueButton;
