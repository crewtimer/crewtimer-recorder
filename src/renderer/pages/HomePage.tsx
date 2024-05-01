import { Box, FormControlLabel, Checkbox } from '@mui/material';
import { useHello, useWorld } from '../util/UseSettings';
import { startRecording, stopRecording } from '../recorder/RecorderApi';

const HomePage = () => {
  const [hello, setHello] = useHello();
  const [world, setWorld] = useWorld();

  return (
    <Box>
      <div>Hello World</div>
      <FormControlLabel
        labelPlacement="end"
        label="Hello"
        control={
          <Checkbox
            checked={hello}
            onChange={() => {
              (hello ? stopRecording : startRecording)()
                .then((result) => console.log(JSON.stringify(result)))
                .catch((err) => console.error(err));
              setHello((current) => !current);
            }}
          />
        }
      />
      <FormControlLabel
        labelPlacement="end"
        label="World"
        control={
          <Checkbox
            checked={world}
            onChange={() => {
              setWorld((current) => !current);
            }}
          />
        }
      />
    </Box>
  );
};
export default HomePage;
