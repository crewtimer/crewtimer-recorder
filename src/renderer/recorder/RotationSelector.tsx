import React from 'react';
import { MenuItem, TextField } from '@mui/material';
import {
  useGuide,
  useRecordingProps,
  useRecordingPropsPending,
} from './RecorderData';

const RotationSelector: React.FC = () => {
  const [recordingProps, setRecordingProps] = useRecordingProps();
  const [, setGuide] = useGuide();
  const [, setRecordingPropsPending] = useRecordingPropsPending();

  const handleChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    setRecordingPropsPending(true);
    setRecordingProps({
      ...recordingProps,
      rotation: Number(event.target.value) as -90 | 0 | 90,
      cropArea: { x: 0, y: 0, width: 1, height: 1 },
    });
    setGuide({ pt1: 0, pt2: 0 });
  };

  return (
    <TextField
      select
      margin="dense"
      label="Rotation"
      size="small"
      value={recordingProps.rotation || 0}
      onChange={handleChange}
      sx={{ minWidth: 100 }}
    >
      <MenuItem value={0}>0°</MenuItem>
      <MenuItem value={-90}>-90°</MenuItem>
      <MenuItem value={90}>+90°</MenuItem>
    </TextField>
  );
};

export default RotationSelector;
