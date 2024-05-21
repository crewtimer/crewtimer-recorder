import ListItem from '@mui/material/ListItem';
import ListItemIcon from '@mui/material/ListItemIcon';
import ListItemText from '@mui/material/ListItemText';
import SettingsIcon from '@mui/icons-material/Settings';
import ListAltIcon from '@mui/icons-material/ListAlt';
import VideocamOutlinedIcon from '@mui/icons-material/VideocamOutlined';
import { List, Tooltip } from '@mui/material';
import { useNavigate } from 'react-router-dom';

function SideNavMenu() {
  const navigate = useNavigate();
  return (
    <List>
      <Tooltip title="Settings">
        <ListItem>
          <ListItemIcon onClick={() => navigate('/home')}>
            <SettingsIcon />
          </ListItemIcon>
          <ListItemText primary="Settings" />
        </ListItem>
      </Tooltip>
      <Tooltip title="Video">
        <ListItem>
          <ListItemIcon onClick={() => navigate('/video')}>
            <VideocamOutlinedIcon />
          </ListItemIcon>
          <ListItemText primary="Video" />
        </ListItem>
      </Tooltip>
      <Tooltip title="Event Log">
        <ListItem>
          <ListItemIcon onClick={() => navigate('/log')}>
            <ListAltIcon />
          </ListItemIcon>
          <ListItemText primary="Event Log" />
        </ListItem>
      </Tooltip>
    </List>
  );
}

export default SideNavMenu;
