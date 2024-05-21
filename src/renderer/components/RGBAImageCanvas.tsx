import React, { useEffect, useRef } from 'react';
import { Box } from '@mui/material';
import {
  useFrameGrab,
  useGuide,
  useIsRecording,
  useRecordingProps,
} from '../recorder/RecorderData';
import {
  requestVideoFrame,
  startRecording,
  stopRecording,
} from '../recorder/RecorderApi';
import generateTestPattern from '../util/ImageUtils';
import { GrabFrameResponse } from '../recorder/RecorderTypes';
import { showErrorDialog } from './ErrorDialog';

const drawIcon = (
  ctx: CanvasRenderingContext2D,
  canvasWidth: number,
  canvasHeight: number,
  playing: boolean,
) => {
  const centerX = canvasWidth / 2;
  const centerY = canvasHeight / 2;
  const size = 50; // Size of the icon
  const radius = size; // Radius of the enclosing circle

  // Draw enclosing circle
  ctx.beginPath();
  ctx.arc(centerX, centerY, radius, 0, 2 * Math.PI);
  ctx.fillStyle = '#44444480';
  ctx.fill();

  // Set icon color and style
  ctx.fillStyle = '#ffffffb0';
  ctx.beginPath();
  if (playing) {
    // Draw stop icon as a square
    ctx.rect(
      centerX - size / 3,
      centerY - size / 3,
      (2 * size) / 3,
      (2 * size) / 3,
    );
  } else {
    // Draw play icon as a triangle
    ctx.moveTo(centerX - size / 4, centerY - size / 2);
    ctx.lineTo(centerX - size / 4, centerY + size / 2);
    ctx.lineTo(centerX + size / 2, centerY);
    ctx.closePath();
  }
  ctx.fill();
};

interface CanvasProps {
  divwidth: number;
  divheight: number;
}
let lastGoodFrame: GrabFrameResponse = generateTestPattern();

const RGBAImageCanvas: React.FC<CanvasProps> = ({ divwidth, divheight }) => {
  let [frame] = useFrameGrab();
  const [guide] = useGuide();
  const [isRecording] = useIsRecording();
  const [settings] = useRecordingProps();
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  if (frame?.data) {
    lastGoodFrame = frame;
  } else {
    frame = lastGoodFrame;
  }
  const togglePlay = () => {
    (isRecording ? stopRecording() : startRecording()).catch(showErrorDialog);
  };

  useEffect(() => {
    const timer = setInterval(() => {
      requestVideoFrame().catch(showErrorDialog);
    }, 200);
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

    if (settings.showFinishGuide) {
      // Draw the red line
      ctx.beginPath();
      ctx.moveTo(offsetX + scaledWidth / 2 + guide.pt1 * scale, 0); // Horizontal center + N pixels offset, scaled
      ctx.lineTo(
        offsetX + scaledWidth / 2 + guide.pt2 * scale,
        offsetY + scaledHeight,
      );
      ctx.strokeStyle = 'red';
      ctx.lineWidth = 1; // Line width can be adjusted as needed
      ctx.stroke();
    }
    drawIcon(ctx, parentWidth, scaledHeight, isRecording);
  }, [
    frame,
    divwidth,
    divheight,
    guide,
    isRecording,
    settings.showFinishGuide,
  ]);

  return (
    <Box sx={{ width: divwidth, height: divheight }} onClick={togglePlay}>
      <canvas ref={canvasRef} style={{ width: '100%', height: '100%' }} />
    </Box>
  );
};

export default RGBAImageCanvas;
