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
import { snooze } from '../util/Util';

interface ViscaValueButtonProps {
  title: string;
  decrement: ViscaCommand;
  increment: ViscaCommand;
  reset: ViscaCommand;
  value?: boolean;
  autoOn?: ViscaCommand;
  autoOff?: ViscaCommand;
  autoOnce?: ViscaCommand;
  stepButtonsAfterMode?: boolean;
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
  stepButtonsAfterMode = false,
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
      await snooze(50);
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
      await snooze(50);
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

  const stepButtons = (
    <>
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
            border: '1px solid rgba(0,0,0,0.23)',
            '&:active': { backgroundColor: 'rgba(0, 0, 0, 0.20)' },
          }}
        >
          <RemoveIcon fontSize="small" />
        </IconButton>
      </Grid>
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
            border: '1px solid rgba(0,0,0,0.23)',
            '&:active': { backgroundColor: 'rgba(0, 0, 0, 0.20)' },
          }}
        >
          <AddIcon fontSize="small" />
        </IconButton>
      </Grid>
    </>
  );

  const modeButtons = autoOn ? (
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
  ) : null;

  return (
    <Grid
      container
      direction="row"
      wrap="nowrap"
      alignItems="center"
      justifyContent="flex-start"
      columnGap={1}
      sx={{ width: 'fit-content', margin: 0 }}
    >
      {title && (
        <Grid item sx={{ width: '50px' }}>
          <Typography variant="subtitle2" noWrap>
            {title}
          </Typography>
        </Grid>
      )}
      {stepButtonsAfterMode && modeButtons}
      {stepButtons}
      {!stepButtonsAfterMode && modeButtons}
    </Grid>
  );
};

export default ViscaValueButton;
