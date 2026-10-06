import React, { useState } from 'react';
import {
  Table,
  TableBody,
  TableCell,
  TableContainer,
  TableHead,
  TableRow,
  Paper,
  Box,
  Button,
  Chip,
  Stack,
  TextField,
  Typography,
  InputAdornment,
} from '@mui/material';
import { alpha } from '@mui/material/styles';
import SearchIcon from '@mui/icons-material/Search';
import { useSystemLog } from '../recorder/RecorderData';
import { RecordingLogEntry } from '../recorder/RecorderTypes';
import { monoFont } from '../components/Panel';
import { setToast } from '../components/Toast';

type Level = 'Error' | 'Warning' | 'Info';
const levelColor: Record<Level, string> = {
  Error: 'error.main',
  Warning: 'warning.main',
  Info: 'text.secondary',
};

/** Frame gaps and timestamp problems reported by the NDI/SRT readers and encoder. */
export const isGapEvent = (entry: RecordingLogEntry) =>
  /Gap=|Duplicate frame timestamp|Timestamp discontinuity|timestamps must increase/.test(
    entry.message,
  );

export const eventLevel = (entry: RecordingLogEntry): Level => {
  if (entry.message.startsWith('Error')) return 'Error';
  if (entry.message.startsWith('Warning') || isGapEvent(entry)) {
    return 'Warning';
  }
  return 'Info';
};

const formatTime = (tsMilli: number): string => {
  const date = new Date(tsMilli);
  const hours = date.getHours().toString().padStart(2, '0');
  const minutes = date.getMinutes().toString().padStart(2, '0');
  const seconds = date.getSeconds().toString().padStart(2, '0');
  const milliseconds = date.getMilliseconds().toString().padStart(3, '0');

  return `${hours}:${minutes}:${seconds}.${milliseconds}`;
};

const formatMessage = (entry: RecordingLogEntry) =>
  entry.count && entry.count > 1
    ? `${entry.message} (×${entry.count})`
    : entry.message;

const filters: { label: string; test: (e: RecordingLogEntry) => boolean }[] = [
  { label: 'All', test: () => true },
  { label: 'Errors', test: (e) => eventLevel(e) === 'Error' },
  { label: 'Warnings', test: (e) => eventLevel(e) === 'Warning' },
  { label: 'Gaps & timing', test: isGapEvent },
];

const LevelLabel = ({ level }: { level: Level }) => (
  <Stack direction="row" alignItems="center" spacing={0.75}>
    <Box
      sx={{
        width: 8,
        height: 8,
        borderRadius: 0.5,
        bgcolor: levelColor[level],
      }}
    />
    <Typography
      component="span"
      sx={{ fontSize: 13, fontWeight: 600, color: levelColor[level] }}
    >
      {level}
    </Typography>
  </Stack>
);

const RecordingLogTable: React.FC = () => {
  const [log, setLogEntries] = useSystemLog();
  const entries = log.filter((entry) => entry.subsystem !== 'Debug').reverse();
  const [filter, setFilter] = useState(filters[0]);
  const [search, setSearch] = useState('');

  const needle = search.toLowerCase();
  const rows = entries.filter(
    (entry) =>
      filter.test(entry) &&
      `${entry.subsystem} ${entry.message}`.toLowerCase().includes(needle),
  );

  const handleClearLogs = () => setLogEntries([]);

  const handleCopy = () => {
    const text = rows
      .map(
        (e) =>
          `${formatTime(e.tsMilli)}\t${eventLevel(e)}\t${e.subsystem}\t${formatMessage(e)}`,
      )
      .join('\n');
    navigator.clipboard
      .writeText(
        `CrewTimer Video Recorder ${window.platform.appVersion}\n${text}`,
      )
      .then(() => setToast({ severity: 'info', msg: 'Event log copied' }))
      .catch(() =>
        setToast({ severity: 'error', msg: 'Could not copy event log' }),
      );
  };

  return (
    <Stack spacing={2} sx={{ maxWidth: 1280, mx: 'auto' }}>
      <Stack
        direction="row"
        flexWrap="wrap"
        gap={1.5}
        alignItems="center"
        justifyContent="space-between"
      >
        <Stack
          direction="row"
          flexWrap="wrap"
          gap={0.75}
          role="group"
          aria-label="Filter events"
        >
          {filters.map((f) => (
            <Chip
              key={f.label}
              label={`${f.label} ${entries.filter(f.test).length}`}
              color={f === filter ? 'primary' : 'default'}
              variant={f === filter ? 'filled' : 'outlined'}
              aria-pressed={f === filter}
              onClick={() => setFilter(f)}
            />
          ))}
        </Stack>
        <Stack direction="row" flexWrap="wrap" gap={1}>
          <TextField
            size="small"
            type="search"
            placeholder="Search messages"
            value={search}
            onChange={(e) => setSearch(e.target.value)}
            InputProps={{
              inputProps: { 'aria-label': 'Search events' },
              startAdornment: (
                <InputAdornment position="start">
                  <SearchIcon fontSize="small" />
                </InputAdornment>
              ),
            }}
          />
          <Button variant="outlined" onClick={handleCopy}>
            Copy for support
          </Button>
          <Button variant="outlined" color="inherit" onClick={handleClearLogs}>
            Clear
          </Button>
        </Stack>
      </Stack>
      <TableContainer component={Paper} variant="outlined">
        <Table size="small" aria-label="Event log">
          <TableHead>
            <TableRow>
              <TableCell sx={{ width: 130 }}>Time</TableCell>
              <TableCell sx={{ width: 110 }}>Level</TableCell>
              <TableCell sx={{ width: 110 }}>Source</TableCell>
              <TableCell>Message</TableCell>
            </TableRow>
          </TableHead>
          <TableBody>
            {rows.map((entry) => {
              const level = eventLevel(entry);
              return (
                <TableRow
                  key={`${entry.tsMilli}-${entry.subsystem}-${entry.message}`}
                  sx={
                    level === 'Error'
                      ? {
                          bgcolor: (theme) =>
                            alpha(theme.palette.error.main, 0.1),
                        }
                      : undefined
                  }
                >
                  <TableCell
                    sx={{ fontFamily: monoFont, color: 'text.secondary' }}
                  >
                    {formatTime(entry.tsMilli)}
                  </TableCell>
                  <TableCell>
                    <LevelLabel level={level} />
                  </TableCell>
                  <TableCell sx={{ color: 'text.secondary' }}>
                    {entry.subsystem}
                  </TableCell>
                  <TableCell sx={{ fontFamily: monoFont, fontSize: 13 }}>
                    {formatMessage(entry)}
                  </TableCell>
                </TableRow>
              );
            })}
            {rows.length === 0 && (
              <TableRow>
                <TableCell colSpan={4} align="center" sx={{ py: 4 }}>
                  <Typography color="text.secondary">
                    No events to show.
                  </Typography>
                </TableCell>
              </TableRow>
            )}
          </TableBody>
        </Table>
      </TableContainer>
    </Stack>
  );
};

export default RecordingLogTable;
