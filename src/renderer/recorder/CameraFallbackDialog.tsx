import { Link, Typography } from '@mui/material';
import { setDialogConfig } from '../components/ConfirmDialog';

export const CAMERA_FALLBACK_IP = '192.168.1.188';

export const showCameraFallbackDialog = (handleConfirm?: () => void) => {
  setDialogConfig({
    title: 'Camera IP Address Fallback',
    body: (
      <>
        <Typography paragraph>
          The selected camera is using the fallback IP address{' '}
          {CAMERA_FALLBACK_IP}. The camera did not detect a DHCP server on the
          network. The camera must be connected to a network with a DHCP server
          to obtain an IP address.
        </Typography>
        <Link
          href="https://crewtimer.com/help/VideoRecorder#internet-connectivity"
          target="_blank"
          rel="noopener noreferrer"
        >
          Help with internet connectivity and camera network setup
        </Link>
      </>
    ),
    button: handleConfirm ? 'Start Recording' : 'OK',
    showCancel: !!handleConfirm,
    handleConfirm,
  });
};
