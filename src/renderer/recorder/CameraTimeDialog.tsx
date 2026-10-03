import { Link } from '@mui/material';
import { setDialogConfig } from '../components/ConfirmDialog';

export const showCameraTimeDialog = (handleConfirm?: () => void) => {
  setDialogConfig({
    title: 'Camera Time Mismatch',
    message:
      'The time shown in the preview differs from the app’s system time by more than 2 minutes. Check the timezone of the computer and ensure the camera has access to the Internet to access an NTP time server.',
    body: (
      <Link
        href="https://crewtimer.com/help/VideoRecorder#internet-connectivity"
        target="_blank"
        rel="noopener noreferrer"
      >
        Help with internet connectivity and camera network setup
      </Link>
    ),
    button: handleConfirm ? 'Start Recording' : 'OK',
    showCancel: !!handleConfirm,
    handleConfirm,
  });
};
