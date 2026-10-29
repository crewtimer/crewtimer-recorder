import React, { useEffect, useState, useRef } from 'react';
import { Stack, ToggleButtonGroup, ToggleButton } from '@mui/material';
import { sendViscaCommand, ViscaCommand } from './ViscaAPI';
import { setToast } from '../components/Toast';
import { snooze } from '../util/Util';
import { StepButton } from './RangeStepper';

type FocusMode = 'auto' | 'man' | 'once' | 'distance';

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
  /** Called when a step button is pressed; the step waits for a returned promise. */
  onPress?: (direction: 'down' | 'up') => void | Promise<void>;
  /** When defined, a Distance mode is offered; true while it is selected. */
  distance?: boolean;
  /** Called when a mode button is chosen; the mode's command waits for a returned promise. */
  onMode?: (mode: FocusMode) => void | Promise<void>;
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
  onPress,
  distance,
  onMode,
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

  // The camera stays in autofocus through a step, and autofocus would undo it.
  const leaveAuto = () => {
    if (isAuto) handleCommand(autoOff);
    setIsAuto(false);
  };

  // Whether a step button is held; it can be released while the press waits on onPress.
  const held = useRef(false);

  const handlePress = async (
    direction: 'down' | 'up',
    command: ViscaCommand,
  ) => {
    held.current = true;
    leaveAuto();
    // The owner may need to finish its own lens move first.
    await onPress?.(direction);

    if (setTimeoutRef.current) {
      clearTimeout(setTimeoutRef.current);
    }
    // Send a quick step and then stop
    const sendStep = async () => {
      await handleCommand(command);
      await snooze(50);
      await handleCommand(reset);
    };
    sendStep();
    // After a while, start again until released
    if (held.current) {
      setTimeoutRef.current = setTimeout(() => handleCommand(command), 300);
    }
  };

  const handleRelease = () => {
    held.current = false;
    if (setTimeoutRef.current) {
      clearTimeout(setTimeoutRef.current);
      setTimeoutRef.current = null;
    }
    // Finally send reset command
    handleCommand(reset);
  };

  // Toggle auto/manual/once
  const handleToggleMode = async (
    _event: React.MouseEvent<HTMLElement>,
    newValue: FocusMode | null,
  ) => {
    // The owner may need to finish its own lens move first.
    if (newValue) await onMode?.(newValue);
    switch (newValue) {
      case 'auto':
        handleCommand(autoOn);
        setIsAuto(true);
        break;
      case 'man':
      case 'distance':
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

  let mode: FocusMode = isAuto ? 'auto' : 'man';
  if (distance) mode = 'distance';

  return (
    <Stack direction="row" alignItems="center" spacing={0.75}>
      {autoOn && (
        <ToggleButtonGroup
          exclusive
          value={mode}
          onChange={handleToggleMode}
          size="small"
          color="primary"
          aria-label={`${name} mode`}
          sx={{ height: 36, mr: 0.5 }}
        >
          <ToggleButton value="auto">Auto</ToggleButton>
          <ToggleButton value="man">Manual</ToggleButton>
          <ToggleButton value="once">Once</ToggleButton>
          {distance !== undefined && (
            <ToggleButton value="distance">Distance</ToggleButton>
          )}
        </ToggleButtonGroup>
      )}
      <StepButton
        direction="down"
        aria-label={`${name} out`}
        onMouseDown={() => handlePress('down', decrement)}
        onMouseUp={handleRelease}
        onTouchStart={() => handlePress('down', decrement)}
        onTouchEnd={handleRelease}
      />
      <StepButton
        direction="up"
        aria-label={`${name} in`}
        onMouseDown={() => handlePress('up', increment)}
        onMouseUp={handleRelease}
        onTouchStart={() => handlePress('up', increment)}
        onTouchEnd={handleRelease}
      />
    </Stack>
  );
};

export default ViscaValueButton;
