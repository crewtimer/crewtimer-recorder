import ListItem from '@mui/material/ListItem';
import ListItemIcon from '@mui/material/ListItemIcon';
import ListItemText from '@mui/material/ListItemText';
import SettingsIcon from '@mui/icons-material/Settings';
import ListAltIcon from '@mui/icons-material/ListAlt';
import VideocamOutlinedIcon from '@mui/icons-material/VideocamOutlined';
import { List, Tooltip } from '@mui/material';
import { useSelectedPage } from '../pages/MainPage';

function SideNavMenu() {
  const [, setSelectedPage] = useSelectedPage();
  return (
    <List>
      <Tooltip title="Settings" placement="right">
        <ListItem>
          <ListItemIcon onClick={() => setSelectedPage('/home')}>
            <SettingsIcon />
          </ListItemIcon>
          <ListItemText primary="Settings" />
        </ListItem>
      </Tooltip>
      <Tooltip title="Video" placement="right">
        <ListItem>
          <ListItemIcon
            onClick={() => {
              setSelectedPage('/video');
            }}
          >
            <VideocamOutlinedIcon />
          </ListItemIcon>
          <ListItemText primary="Video" />
        </ListItem>
      </Tooltip>
      <Tooltip title="Event Log" placement="right">
        <ListItem>
          <ListItemIcon
            onClick={() => {
              setSelectedPage('/log');
            }}
          >
            <ListAltIcon />
          </ListItemIcon>
          <ListItemText primary="Event Log" />
        </ListItem>
      </Tooltip>
    </List>
  );
}

export default SideNavMenu;
