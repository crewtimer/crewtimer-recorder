import React from 'react';
import {
  Table,
  TableBody,
  TableCell,
  TableContainer,
  TableHead,
  TableRow,
  Paper,
  IconButton,
  Box,
  Typography,
} from '@mui/material';
import DeleteIcon from '@mui/icons-material/DeleteOutline';
import { useLoggerAlert, useSystemLog } from '../recorder/RecorderData';

const formatTime = (tsMilli: number): string => {
  const date = new Date(tsMilli);
  const hours = date.getHours().toString().padStart(2, '0');
  const minutes = date.getMinutes().toString().padStart(2, '0');
  const seconds = date.getSeconds().toString().padStart(2, '0');
  const milliseconds = date.getMilliseconds().toString().padStart(3, '0');

  return `${hours}:${minutes}:${seconds}.${milliseconds}`;
};

const RecordingLogTable: React.FC = () => {
  const [entries, setLogEntries] = useSystemLog();
  const [, setLoggerAlert] = useLoggerAlert();
  const itemsToShow = entries.filter((entry) => entry.subsystem !== 'Debug');

  const handleClearLogs = () => {
    setLogEntries([]); // Clear the table data
    setLoggerAlert(0);
  };

  return (
    <Paper>
      <Box display="flex" justifyContent="space-between" alignItems="center">
        <Typography variant="h6">Log Events</Typography>
        <IconButton onClick={handleClearLogs} color="primary">
          <DeleteIcon />
        </IconButton>
      </Box>
      <TableContainer>
        <Table aria-label="simple table" size="small">
          <TableHead>
            <TableRow>
              <TableCell>Time</TableCell>
              <TableCell>System</TableCell>
              <TableCell>Message</TableCell>
            </TableRow>
          </TableHead>
          <TableBody>
            {itemsToShow.reverse().map((entry, entryIndex) => {
              const key = `${entryIndex}-${entry.tsMilli}-${entry.message}`;
              const styles = entry.message.startsWith('Error')
                ? { color: 'white', background: 'red' }
                : undefined;
              return (
                <TableRow key={key}>
                  <TableCell sx={styles}>{formatTime(entry.tsMilli)}</TableCell>
                  <TableCell sx={styles}>{entry.subsystem}</TableCell>
                  <TableCell sx={styles}>{entry.message}</TableCell>
                </TableRow>
              );
            })}
          </TableBody>
        </Table>
      </TableContainer>
    </Paper>
  );
};

export default RecordingLogTable;
