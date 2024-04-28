import ListItem from '@mui/material/ListItem';
import ListItemIcon from '@mui/material/ListItemIcon';
import ListItemText from '@mui/material/ListItemText';
import HomeIcon from '@mui/icons-material/Home';
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
        <ListItemText primary="Home" />
      </ListItem>
    </List>
  );
}

export default SideNavMenu;
