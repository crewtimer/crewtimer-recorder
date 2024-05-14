import React, { useEffect, useRef } from 'react';
import { Box } from '@mui/material';
import { useFrameGrab } from '../recorder/RecorderData';
import { requestVideoFrame } from '../recorder/RecorderApi';

interface CanvasProps {
  divwidth: number;
  divheight: number;
}

const RGBAImageCanvas: React.FC<CanvasProps> = ({ divwidth, divheight }) => {
  const [frame] = useFrameGrab();
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  useEffect(() => {
    const timer = setInterval(() => {
      requestVideoFrame().catch((err) => console.error(err));
    }, 100);
    return () => clearInterval(timer);
  }, []);

  useEffect(() => {
    if (
      !canvasRef.current ||
      !frame ||
      !frame.data ||
      !frame.width ||
      !frame.height
    ) {
      return;
    }

    const { data, width, height } = frame;
    const canvas = canvasRef.current;
    const ctx = canvas.getContext('2d');

    if (!ctx) {
      return;
    }

    // Adjust the canvas size to fill its parent
    const parentWidth = canvas.parentElement?.clientWidth || width;
    const parentHeight = canvas.parentElement?.clientHeight || height;

    // Calculate the scale to maintain aspect ratio and fit within the parent container
    const scale = Math.min(parentWidth / width, parentHeight / height);

    const scaledWidth = width * scale;
    const scaledHeight = height * scale;

    // Center the image in the canvas
    const offsetX = (parentWidth - scaledWidth) / 2;
    const offsetY = 0; // (parentHeight - scaledHeight) / 2;

    canvas.width = parentWidth;
    canvas.height = parentHeight;

    // Create an ImageData object
    const imageData = new ImageData(new Uint8ClampedArray(data), width, height);

    // Create an offscreen canvas to hold the image at its original size
    const offscreenCanvas = document.createElement('canvas');
    offscreenCanvas.width = width;
    offscreenCanvas.height = height;
    const offscreenCtx = offscreenCanvas.getContext('2d');
    offscreenCtx?.putImageData(imageData, 0, 0);

    // Clear the canvas and draw the offscreen canvas to the main canvas with scaling
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.drawImage(
      offscreenCanvas,
      0,
      0,
      width,
      height,
      offsetX,
      offsetY,
      scaledWidth,
      scaledHeight,
    );
  }, [frame, divwidth, divheight]);

  return (
    <Box sx={{ width: divwidth, height: divheight }}>
      <canvas
        ref={canvasRef}
        style={{ width: '100%', height: '100%', border: '1px solid black' }}
      />
    </Box>
  );
};

export default RGBAImageCanvas;
