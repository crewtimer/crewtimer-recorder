import ListItem from '@mui/material/ListItem';
import ListItemIcon from '@mui/material/ListItemIcon';
import ListItemText from '@mui/material/ListItemText';
import HomeIcon from '@mui/icons-material/Settings';
import SettingsIcon from '@mui/icons-material/History';
import VideocamOutlinedIcon from '@mui/icons-material/VideocamOutlined';
import { List } from '@mui/material';
import { useNavigate } from 'react-router-dom';

function SideNavMenu() {
  const navigate = useNavigate();
  return (
    <List>
      <ListItem>
        <ListItemIcon onClick={() => navigate('/home')}>
          <HomeIcon />
        </ListItemIcon>
        <ListItemText primary="Settings" />
      </ListItem>
      <ListItem>
        <ListItemIcon onClick={() => navigate('/video')}>
          <VideocamOutlinedIcon />
        </ListItemIcon>
        <ListItemText primary="Video" />
      </ListItem>
      <ListItem>
        <ListItemIcon onClick={() => navigate('/log')}>
          <SettingsIcon />
        </ListItemIcon>
        <ListItemText primary="Log" />
      </ListItem>
    </List>
  );
}

export default SideNavMenu;
