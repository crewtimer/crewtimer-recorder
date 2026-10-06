import React, { useEffect, useState, useRef } from 'react';
import { Stack, ToggleButtonGroup, ToggleButton } from '@mui/material';
import { sendViscaCommand, ViscaCommand } from './ViscaAPI';
import { setToast } from '../components/Toast';
import { snooze } from '../util/Util';
import { StepButton } from './RangeStepper';

interface ViscaValueButtonProps {
  /** Used in the step buttons' accessible names, e.g. "Zoom in". */
  name: string;
  decrement: ViscaCommand;
  increment: ViscaCommand;
  reset: ViscaCommand;
  value?: boolean;
  autoOn?: ViscaCommand;
  autoOff?: ViscaCommand;
  autoOnce?: ViscaCommand;
}

const ViscaValueButton: React.FC<ViscaValueButtonProps> = ({
  name,
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

  return (
    <Stack direction="row" alignItems="center" spacing={0.75}>
      {autoOn && (
        <ToggleButtonGroup
          exclusive
          value={isAuto ? 'auto' : 'man'}
          onChange={handleToggleMode}
          size="small"
          color="primary"
          aria-label={`${name} mode`}
          sx={{ height: 36, mr: 0.5 }}
        >
          <ToggleButton value="auto">Auto</ToggleButton>
          <ToggleButton value="man">Manual</ToggleButton>
          <ToggleButton value="once">Once</ToggleButton>
        </ToggleButtonGroup>
      )}
      <StepButton
        direction="down"
        aria-label={`${name} out`}
        onMouseDown={handleDecrementPress}
        onMouseUp={handleDecrementRelease}
        onTouchStart={handleDecrementPress}
        onTouchEnd={handleDecrementRelease}
      />
      <StepButton
        direction="up"
        aria-label={`${name} in`}
        onMouseDown={handleIncrementPress}
        onMouseUp={handleIncrementRelease}
        onTouchStart={handleIncrementPress}
        onTouchEnd={handleIncrementRelease}
      />
    </Stack>
  );
};

export default ViscaValueButton;
