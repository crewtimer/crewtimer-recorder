import React, { useEffect, useState } from 'react';
import {
  Table,
  TableBody,
  TableCell,
  TableContainer,
  TableHead,
  TableRow,
  Paper,
} from '@mui/material';
import { RecordingLogEntry, queryRecordingLog } from '../recorder/RecorderApi';

const formatTime = (tsMilli: number): string => {
  const date = new Date(tsMilli);
  const hours = date.getHours().toString().padStart(2, '0');
  const minutes = date.getMinutes().toString().padStart(2, '0');
  const seconds = date.getSeconds().toString().padStart(2, '0');
  const milliseconds = date.getMilliseconds().toString().padStart(3, '0');

  return `${hours}:${minutes}:${seconds}.${milliseconds}`;
};

const RecordingLogTable: React.FC = () => {
  const [entries, setEntries] = useState<RecordingLogEntry[]>([]);

  useEffect(() => {
    queryRecordingLog()
      .then((result) => setEntries(result.list || []))
      .catch(console.error);
  }, []);
  return (
    <TableContainer component={Paper}>
      <Table aria-label="simple table" size="small">
        <TableHead>
          <TableRow>
            <TableCell>Time</TableCell>
            <TableCell>Sub System</TableCell>
            <TableCell>Message</TableCell>
          </TableRow>
        </TableHead>
        <TableBody>
          {entries.map((entry) => (
            <TableRow key={`${entry.tsMilli}-${entry.message}`}>
              <TableCell>{formatTime(entry.tsMilli)}</TableCell>
              <TableCell>{entry.subsystem}</TableCell>
              <TableCell>{entry.message}</TableCell>
            </TableRow>
          ))}
        </TableBody>
      </Table>
    </TableContainer>
  );
};

export default RecordingLogTable;
