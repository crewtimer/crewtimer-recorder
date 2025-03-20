import React from 'react';
import { IconButton, Badge } from '@mui/material';
import NotificationsIcon from '@mui/icons-material/Notifications';
import { useLoggerAlert } from '../recorder/RecorderData';
import { useSelectedPage } from '../pages/MainPage';

const LoggerAlert: React.FC = () => {
  // Assuming useLoggerAlert returns a tuple [alertCount, setAlertCount]
  const [alertCount, setAlertCount] = useLoggerAlert();
  const [, setSelectedPage] = useSelectedPage();

  const handleClick = () => {
    setAlertCount(0);
    setSelectedPage('/log');
  };

  if (alertCount === 0) {
    // eslint-disable-next-line react/jsx-no-useless-fragment
    return <></>;
  }

  return (
    <IconButton color="inherit" onClick={handleClick}>
      <Badge badgeContent={alertCount} color="error">
        <NotificationsIcon />
      </Badge>
    </IconButton>
  );
};

export default LoggerAlert;
