import React, { useEffect, useState, useRef } from 'react';
import {
  IconButton,
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
  const setTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    setIsAuto(value === true);
  }, [value]);

  // Cleanup: clear timeouts if component unmounts while button is still pressed
  useEffect(() => {
    return () => {
      if (setTimeoutRef.current) {
        clearTimeout(setTimeoutRef.current);
        setTimeoutRef.current = null;
      }
    };
  }, []);

  const handleCommand = async (command: ViscaCommand | undefined) => {
    if (!command) return;
    try {
      await sendViscaCommand(command);
    } catch (error) {
      setToast({
        severity: 'error',
        msg: `Error sending VISCA command: ${error}`,
      });
      console.error('Error sending VISCA command:', error);
    }
  };

  // Decrement press/release handlers
  const handleDecrementPress = () => {
    setIsAuto(false); // Turn off auto

    // Send a quick decrement and then stop
    const sendDecrement = async () => {
      await handleCommand(decrement);
      await handleCommand(reset);
    };

    sendDecrement();
    // After a while, start again until released
    setTimeoutRef.current = setTimeout(() => handleCommand(decrement), 300);
  };

  const handleDecrementRelease = () => {
    if (setTimeoutRef.current) {
      clearTimeout(setTimeoutRef.current);
      setTimeoutRef.current = null;
    }
    // Finally send reset command
    handleCommand(reset);
  };

  // Increment press/release handlers
  const handleIncrementPress = () => {
    setIsAuto(false);

    // Send a quick increment and then stop
    const sendIncrement = async () => {
      await handleCommand(increment);
      await handleCommand(reset);
    };

    if (setTimeoutRef.current) {
      clearTimeout(setTimeoutRef.current);
    }
    sendIncrement();
    // After a while, start again until released
    setTimeoutRef.current = setTimeout(() => handleCommand(increment), 300); // 100 ms
  };

  const handleIncrementRelease = () => {
    if (setTimeoutRef.current) {
      clearTimeout(setTimeoutRef.current);
      setTimeoutRef.current = null;
    }
    handleCommand(reset);
  };

  // Toggle auto/manual/once
  const handleToggleMode = (
    _event: React.MouseEvent<HTMLElement>,
    newValue: 'auto' | 'man' | 'once' | null,
  ) => {
    switch (newValue) {
      case 'auto':
        handleCommand(autoOn);
        setIsAuto(true);
        break;
      case 'man':
        handleCommand(autoOff);
        setIsAuto(false);
        break;
      case 'once':
        handleCommand(autoOnce);
        break;
      default:
        // If newValue is null, do nothing, or customize this as needed
        break;
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
          color="primary"
          sx={{
            padding: '4px',
            // backgroundColor: 'rgba(0, 0, 0, 0.04)',
            border: '1px solid rgba(0,0,0,0.23)',
            '&:active': { backgroundColor: 'rgba(0, 0, 0, 0.20)' },
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
          color="primary"
          sx={{
            padding: '4px',
            // backgroundColor: 'rgba(0, 0, 0, 0.04)',
            border: '1px solid rgba(0,0,0,0.23)',
            '&:active': { backgroundColor: 'rgba(0, 0, 0, 0.20)' },
          }}
        >
          <AddIcon fontSize="small" />
        </IconButton>
      </Grid>

      {/* Toggle Button Group (Optional) */}
      {autoOn && (
        <Grid item>
          <ToggleButtonGroup
            exclusive
            value={isAuto ? 'auto' : 'man'}
            onChange={handleToggleMode}
            size="small"
            sx={{
              '& .MuiToggleButton-root': {
                padding: '2px 6px',
                minWidth: 0,
                borderRadius: '4px',
              },
            }}
          >
            <ToggleButton color="primary" value="auto">
              Auto
            </ToggleButton>
            <ToggleButton color="primary" value="man">
              Man
            </ToggleButton>
            <ToggleButton color="primary" value="once">
              Once
            </ToggleButton>
          </ToggleButtonGroup>
        </Grid>
      )}
    </Grid>
  );
};

export default ViscaValueButton;
