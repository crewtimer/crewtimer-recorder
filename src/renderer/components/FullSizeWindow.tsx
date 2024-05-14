import { Box, Typography } from '@mui/material';
import { FC, useState } from 'react';
import Measure from 'react-measure';

interface WinDimensions {
  divwidth: number;
  divheight: number;
}
export interface FullSizeWindowProps {
  component: FC<WinDimensions>;
}

export const Foo: FC<WinDimensions> = ({ divwidth, divheight }) => {
  return (
    <Box
      sx={{
        position: 'absolute',
        backgroundColor: 'yellow',
        width: divwidth,
        height: divheight,
      }}
    >
      <Typography>{`${divwidth}x${divheight}`}</Typography>
    </Box>
  );
};

export const FullSizeWindow: FC<FullSizeWindowProps> = ({ component }) => {
  const [{ width, height }, setDimensions] = useState({
    top: 170,
    width: 2,
    height: 2,
  });

  const ChildComponent = component;

  return (
    <div
      style={{
        // margin: '16px', // Use state variable for padding
        width: '100%', // Fill the width of the content area
        height: '100%', // Fill the height of the content area
        display: 'flex', // Use flexbox for centering
        justifyContent: 'center', // Center horizontally
        alignItems: 'center', // Center vertically
        overflow: 'hidden', // In case the image is too big
        flexDirection: 'column',
      }}
    >
      <Measure
        bounds
        onResize={(contentRect) => {
          if (contentRect.bounds) {
            setDimensions({
              top: contentRect.bounds.top,
              width: contentRect.bounds.width,
              height: contentRect.bounds.height,
            });
          }
        }}
      >
        {({ measureRef }) => (
          <div ref={measureRef} style={{ flexGrow: 1, width: '100%' }}>
            <div style={{ position: 'absolute', width, height }}>
              <ChildComponent divwidth={width} divheight={height} />
            </div>
          </div>
        )}
      </Measure>
    </div>
  );
};
