import React from 'react';
import { MenuItem, Select, SelectChangeEvent } from '@mui/material';
import {
  useGuide,
  useRecordingProps,
  useRecordingPropsPending,
} from './RecorderData';

const RotationSelector: React.FC = () => {
  const [recordingProps, setRecordingProps] = useRecordingProps();
  const [, setGuide] = useGuide();
  const [, setRecordingPropsPending] = useRecordingPropsPending();

  const handleChange = (event: SelectChangeEvent<number>) => {
    setRecordingPropsPending(true);
    setRecordingProps({
      ...recordingProps,
      rotation: Number(event.target.value) as -180 | -90 | 0 | 90,
      cropArea: { x: 0, y: 0, width: 1, height: 1 },
    });
    setGuide({ pt1: 0, pt2: 0 });
  };

  return (
    <Select
      size="small"
      value={recordingProps.rotation || 0}
      onChange={handleChange}
      inputProps={{ 'aria-label': 'Rotation' }}
      sx={{ height: 36, minWidth: 100 }}
    >
      <MenuItem value={0}>0°</MenuItem>
      <MenuItem value={-90}>-90°</MenuItem>
      <MenuItem value={-180}>-180°</MenuItem>
      <MenuItem value={90}>+90°</MenuItem>
    </Select>
  );
};

export default RotationSelector;
