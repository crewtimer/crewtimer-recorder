import { ReactNode } from 'react';
import { Box, ButtonBase, Stack, Tooltip } from '@mui/material';
import { useRecordingProps } from '../recorder/RecorderData';
import { useCameraList } from '../recorder/CameraMonitor';
import { formatOffset, Tone, useSignalStats } from '../recorder/SignalHealth';
import { useSelectedPage } from '../pages/SelectedPage';

const toneColor = (tone: Tone) => (tone ? `${tone}.main` : 'text.secondary');

const Stat = ({ tone, children }: { tone?: Tone; children: ReactNode }) => (
  <Box component="span" sx={{ color: toneColor(tone) }}>
    {children}
  </Box>
);

const Dot = ({ color }: { color: string }) => (
  <Box
    sx={{
      width: 8,
      height: 8,
      borderRadius: '50%',
      bgcolor: color,
      flexShrink: 0,
    }}
  />
);

/** Always-visible summary of the source and recording health. */
export const StatusBar = () => {
  const [{ networkCamera, protocol, cropArea }] = useRecordingProps();
  const [cameraList] = useCameraList();
  const [, setSelectedPage] = useSelectedPage();
  const stats = useSignalStats();
  const camera = cameraList.find((c) => c.name === networkCamera);

  const cropped = cropArea.width !== 1 || cropArea.height !== 1;
  const cropText = cropped
    ? ` → ${Math.round((cropArea.width * stats.width) / 4) * 4}×${Math.round((cropArea.height * stats.height) / 4) * 4}`
    : '';

  let sourceText = `${networkCamera || 'No camera selected'} · ${protocol}`;
  if (networkCamera && !camera) sourceText += ' · not found';

  return (
    <Box
      component="footer"
      sx={{
        display: 'flex',
        alignItems: 'center',
        gap: 2,
        px: 2.5,
        height: 28,
        flexShrink: 0,
        bgcolor: 'background.paper',
        borderTop: 1,
        borderColor: 'divider',
        overflow: 'hidden',
        whiteSpace: 'nowrap',
        // One font and line box for every item so they share a baseline;
        // tabular digits keep changing numbers from shifting
        fontSize: 12,
        lineHeight: '20px',
        fontVariantNumeric: 'tabular-nums',
      }}
    >
      <Stack
        direction="row"
        alignItems="center"
        spacing={1}
        sx={{ minWidth: 0 }}
      >
        <Dot color={camera ? 'success.main' : 'warning.main'} />
        <Box
          component="span"
          sx={{ overflow: 'hidden', textOverflow: 'ellipsis' }}
        >
          {sourceText}
        </Box>
      </Stack>

      <Stack direction="row" alignItems="center" spacing={2} sx={{ flex: 1 }}>
        {stats.recording && stats.width > 0 && (
          <>
            <Stat>{`${stats.width}×${stats.height}${cropText}`}</Stat>
            <Stat>{`${stats.fps} fps`}</Stat>
            <Tooltip title="Frames waiting to be encoded">
              <span>
                <Stat
                  tone={
                    stats.backlogTone === 'success'
                      ? undefined
                      : stats.backlogTone
                  }
                >
                  {`backlog ${stats.frameBacklog}`}
                </Stat>
              </span>
            </Tooltip>
          </>
        )}
        {camera && stats.offset !== undefined && (
          <Tooltip title="Camera clock vs this PC, including network delay">
            <span>
              <Stat tone={stats.clockOff ? 'error' : undefined}>
                {`clock ${formatOffset(stats.offset)}`}
              </Stat>
            </span>
          </Tooltip>
        )}
      </Stack>

      <ButtonBase
        onClick={() => setSelectedPage('/log')}
        aria-label={`${stats.gapCount} frame gaps, ${stats.errorCount} errors. Open event log`}
        sx={{ height: '100%', px: 1, gap: 2, borderRadius: 1 }}
      >
        <Stack direction="row" alignItems="center" spacing={0.75}>
          <Dot color={stats.gapCount ? 'warning.main' : 'success.main'} />
          <Stat tone={stats.gapCount ? 'warning' : undefined}>
            {`${stats.gapCount} gaps`}
          </Stat>
        </Stack>
        <Stack direction="row" alignItems="center" spacing={0.75}>
          <Dot color={stats.errorCount ? 'error.main' : 'success.main'} />
          <Stat tone={stats.errorCount ? 'error' : undefined}>
            {`${stats.errorCount} errors`}
          </Stat>
        </Stack>
      </ButtonBase>
    </Box>
  );
};
