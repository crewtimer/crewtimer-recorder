import { Box, LinearProgress, Stack, Tooltip, Typography } from '@mui/material';
import { useRecordingDiskUsage } from '../recorder/RecorderData';
import { DISK_WARN_PERCENT, formatBytes } from '../recorder/SignalHealth';

/** Header pill showing how full the recording disk is; red from 85%. */
export const DiskStatus = () => {
  const [disk] = useRecordingDiskUsage();
  if (!disk || disk.error) return null;

  const percent = Math.round(disk.usedPercent);
  const full = percent >= DISK_WARN_PERCENT;
  return (
    <Tooltip
      title={`Recording disk: ${formatBytes(disk.freeBytes)} free of ${formatBytes(disk.totalBytes)}`}
    >
      <Stack
        direction="row"
        alignItems="center"
        spacing={1}
        sx={{
          px: 1.5,
          py: 0.75,
          borderRadius: 999,
          border: 1,
          borderColor: full ? 'error.main' : 'divider',
          bgcolor: full ? 'error.main' : 'action.hover',
          color: full ? 'error.contrastText' : 'text.primary',
        }}
      >
        <Typography
          sx={{
            fontSize: 12,
            fontWeight: 700,
            letterSpacing: '0.08em',
            fontVariantNumeric: 'tabular-nums',
          }}
        >
          {`DISK ${percent}%`}
        </Typography>
        <Box sx={{ width: 40 }}>
          <LinearProgress
            variant="determinate"
            value={Math.min(100, disk.usedPercent)}
            color={full ? 'inherit' : 'success'}
            sx={{ height: 4, borderRadius: 2 }}
          />
        </Box>
      </Stack>
    </Tooltip>
  );
};

export default DiskStatus;
