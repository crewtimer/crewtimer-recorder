import React from 'react';
import { IconButton, Typography, Grid } from '@mui/material';
import RemoveIcon from '@mui/icons-material/Remove';
import AddIcon from '@mui/icons-material/Add';

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
    } else {
      // If you want to clamp the value at `min`, uncomment:
      // onChange(min);
    }
  };

  const handleIncrement = () => {
    const newValue = value + step;
    if (newValue <= max) {
      onChange(newValue);
    } else {
      // If you want to clamp the value at `max`, uncomment:
      // onChange(max);
    }
  };

  return (
    <Grid
      container
      direction="row" // Explicitly use row direction
      wrap="nowrap" // Prevent wrapping to a new line
      alignItems="center"
      justifyContent="center"
      spacing={1}
      sx={{ width: 'fit-content' }}
    >
      {/* Title (fixed width to align multiple panels) */}
      {title && (
        <Grid item sx={{ width: '55px' }}>
          <Typography variant="subtitle2" noWrap>
            {title}
          </Typography>
        </Grid>
      )}
      <Grid item>
        <IconButton
          size="small"
          disabled={step > 0 ? value === min : value === max}
          onClick={handleDecrement}
          color="primary"
          sx={{
            padding: '4px',
            // backgroundColor: 'rgba(0, 0, 0, 0.04)',
            border: '1px solid rgba(0,0,0,0.23)',
            '&:active': {
              backgroundColor: 'rgba(0, 0, 0, 0.20)',
            },
          }}
        >
          <RemoveIcon fontSize="small" />
        </IconButton>
      </Grid>
      <Grid item>
        <IconButton
          disabled={step > 0 ? value === max : value === min}
          size="small"
          onClick={handleIncrement}
          color="primary"
          sx={{
            padding: '4px',
            // backgroundColor: 'rgba(0, 0, 0, 0.04)',
            border: '1px solid rgba(0,0,0,0.23)',
            '&:active': {
              backgroundColor: 'rgba(0, 0, 0, 0.20)',
            },
          }}
        >
          <AddIcon fontSize="small" />
        </IconButton>
      </Grid>
      <Grid item>
        <Typography
          variant="body2"
          sx={{ minWidth: '2rem', textAlign: 'center' }}
        >
          {labels ? labels[value] : value}
        </Typography>
      </Grid>
    </Grid>
  );
};

export default RangeStepper;
