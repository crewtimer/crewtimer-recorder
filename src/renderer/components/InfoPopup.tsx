import React, { ReactNode, useState } from 'react';
import { IconButton, Popover, Typography, Box } from '@mui/material';
import InfoIcon from '@mui/icons-material/Info';

/**
 * InfoPopupProps interface for defining the props accepted by the InfoPopup component.
 */
interface InfoPopupProps {
  /**
   * Optional text to be displayed inside the popover.
   */
  text?: string;
  /**
   * Optional React node to be displayed inside the popover instead of text.
   */
  body?: ReactNode;
}

/**
 * InfoPopup Component
 * This component renders an icon button with an information icon. When clicked,
 * a popover is displayed containing text or custom content. The popover can be closed
 * by clicking either the icon button or inside the popover content.
 *
 * @param {InfoPopupProps} props - The props for the InfoPopup component.
 * @returns {JSX.Element} The rendered InfoPopup component.
 */
const InfoPopup: React.FC<InfoPopupProps> = ({ text, body }) => {
  const [anchorEl, setAnchorEl] = useState<null | HTMLElement>(null);

  /**
   * Handles the click event on the icon button, toggling the popover's visibility.
   *
   * @param {React.MouseEvent<HTMLElement>} event - The click event.
   */
  const handleClick = (event: React.MouseEvent<HTMLElement>) => {
    setAnchorEl(anchorEl ? null : event.currentTarget); // Toggle the popover on click
  };

  /**
   * Closes the popover.
   */
  const handleClose = () => {
    setAnchorEl(null);
  };

  const open = Boolean(anchorEl);
  const id = open ? 'info-popover' : undefined;

  return (
    <div>
      <IconButton onClick={handleClick} aria-describedby={id}>
        <InfoIcon />
      </IconButton>
      <Popover
        id={id}
        open={open}
        anchorEl={anchorEl}
        onClose={handleClose}
        anchorOrigin={{
          vertical: 'bottom',
          horizontal: 'center',
        }}
        transformOrigin={{
          vertical: 'top',
          horizontal: 'center',
        }}
      >
        <Box
          sx={{ padding: 2, cursor: 'pointer' }}
          onClick={handleClose} // Close on click inside the popover
        >
          {body || (text && <Typography>{text}</Typography>)}
        </Box>
      </Popover>
    </div>
  );
};

export default InfoPopup;
