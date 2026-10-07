import { ReactNode } from 'react';
import { Paper, Stack, Typography } from '@mui/material';

export const monoFont = 'ui-monospace, "Cascadia Mono", Consolas, monospace';

interface PanelProps {
  title?: string;
  aside?: ReactNode;
  children: ReactNode;
}

/** Bordered card used for each section of the recorder screens. */
export const Panel = ({ title, aside, children }: PanelProps) => (
  <Paper
    variant="outlined"
    sx={{ p: 2, display: 'flex', flexDirection: 'column', gap: 1.75 }}
  >
    {title && (
      <Stack
        direction="row"
        justifyContent="space-between"
        alignItems="center"
        gap={1}
      >
        <Typography component="h2" sx={{ fontSize: 15, fontWeight: 600 }}>
          {title}
        </Typography>
        {aside}
      </Stack>
    )}
    {children}
  </Paper>
);
