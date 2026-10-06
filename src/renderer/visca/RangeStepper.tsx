import React from 'react';
import { IconButton, IconButtonProps, Stack, Typography } from '@mui/material';
import RemoveIcon from '@mui/icons-material/Remove';
import AddIcon from '@mui/icons-material/Add';
import { monoFont } from '../components/Panel';

/** Square −/+ button shared by the camera control steppers. */
export const StepButton: React.FC<
  Pick<
    IconButtonProps,
    | 'aria-label'
    | 'disabled'
    | 'onClick'
    | 'onMouseDown'
    | 'onMouseUp'
    | 'onTouchStart'
    | 'onTouchEnd'
  > & { direction: 'down' | 'up' }
> = ({
  direction,
  'aria-label': ariaLabel,
  disabled,
  onClick,
  onMouseDown,
  onMouseUp,
  onTouchStart,
  onTouchEnd,
}) => (
  <IconButton
    color="primary"
    aria-label={ariaLabel}
    disabled={disabled}
    onClick={onClick}
    onMouseDown={onMouseDown}
    onMouseUp={onMouseUp}
    onTouchStart={onTouchStart}
    onTouchEnd={onTouchEnd}
    sx={{
      width: 36,
      height: 36,
      border: 1,
      borderColor: 'divider',
      borderRadius: 1,
      '&:active': { bgcolor: 'action.selected' },
    }}
  >
    {direction === 'down' ? (
      <RemoveIcon fontSize="small" />
    ) : (
      <AddIcon fontSize="small" />
    )}
  </IconButton>
);

interface RangeStepperProps {
  title: string;
  min: number;
  max: number;
  /** Current value of the stepper (controlled). */
  value: number;
  labels?: string[];
  /** Callback that receives the new value whenever it changes. */
  onChange: (newValue: number) => void;
  /**
   * How much to increment/decrement by on each click.
   * Default = 1
   */
  step?: number;
}

const RangeStepper: React.FC<RangeStepperProps> = ({
  title,
  min,
  max,
  value,
  labels,
  onChange,
  step = 1,
}) => {
  const handleDecrement = () => {
    const newValue = value - step;
    if (newValue >= min) {
      onChange(newValue);
    }
  };

  const handleIncrement = () => {
    const newValue = value + step;
    if (newValue <= max) {
      onChange(newValue);
    }
  };

  return (
    <Stack direction="row" alignItems="center" spacing={0.75}>
      <Typography variant="body2" color="text.secondary" sx={{ width: 52 }}>
        {title}
      </Typography>
      <StepButton
        direction="down"
        aria-label={`Decrease ${title}`}
        disabled={step > 0 ? value === min : value === max}
        onClick={handleDecrement}
      />
      <StepButton
        direction="up"
        aria-label={`Increase ${title}`}
        disabled={step > 0 ? value === max : value === min}
        onClick={handleIncrement}
      />
      <Typography
        sx={{ fontFamily: monoFont, fontSize: 14, minWidth: 48, pl: 0.5 }}
      >
        {labels ? labels[value] : value}
      </Typography>
    </Stack>
  );
};

export default RangeStepper;
