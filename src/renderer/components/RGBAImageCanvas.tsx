/* eslint-disable no-bitwise */
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

/**
 * Convert a timestamp in milliseconds to a string formatted as HH:MM:SS.MMM
 * @param timestamp UTC timestamp in milliseconds
 * @param tzOffsetMinutes Offset added to timestamp to get desired local time
 * @returns string formmatted as HH:MM:SS.MMM
 */
export function convertTimestampToString(
  utcMillis: number,
  tzOffsetMinutes?: number,
): string {
  // Use the provided tzOffsetMinutes directly if defined, otherwise, use the local timezone offset
  // Note: getTimezoneOffset() returns the offset in minutes as the difference between UTC and local time,
  // which means we add it directly to adjust the UTC time to the desired timezone.
  const offset =
    tzOffsetMinutes !== undefined
      ? tzOffsetMinutes
      : -new Date().getTimezoneOffset();
  const adjustedTime = new Date(utcMillis + offset * 60000);

  // Extract hours, minutes, seconds, and milliseconds
  const hours = adjustedTime.getUTCHours();
  const minutes = adjustedTime.getUTCMinutes();
  const seconds = adjustedTime.getUTCSeconds();
  const milliseconds = adjustedTime.getUTCMilliseconds();

  // Format the time components to ensure correct digit lengths
  const formatTimeComponent = (num: number, length: number = 2) =>
    num.toString().padStart(length, '0');

  // Construct formatted time string including milliseconds
  return `${formatTimeComponent(hours)}:${formatTimeComponent(
    minutes,
  )}:${formatTimeComponent(seconds)}.${formatTimeComponent(milliseconds, 3)}`;
}

/**
 * Extract a 64 bit 100ns UTC timestamp from the video frame.  The timestamp
 * is encoded in the row as two pixels per bit with each bit being white for 1 and black for 0.
 * @param image A rgba image array
 * @param row The row to extract the timestamp from
 * @param width The number of columns in a row
 * @returns The extracted timestamp in milliseconds
 */
function extractTimestampFromFrame(
  image: Uint8Array,
  row: number,
  width: number,
): number {
  let number = 0n; // Initialize the 64-bit number as a BigInt

  for (let col = 0; col < 64; col += 1) {
    const pixel1 = image[4 * (row * width + col * 2)]; // Get the pixel at the current column
    const pixel2 = image[4 * (row * width + col * 2 + 1)];

    // Check the pixel's color values
    const isGreen = pixel1 + pixel2 > 220;
    const bit = isGreen ? 1n : 0n;

    number <<= 1n;

    // Set the corresponding bit in the 64-bit number
    number |= bit;
  }

  // console.log(`${number}`.replace('n', ''));

  number = (5000n + number) / 10000n; // Round 64-bit number to milliseconds

  return Number(number); // Convert the BigInt number to a regular number
}

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
    let timestamp = extractTimestampFromFrame(data, 0, width);
    if (timestamp === 0) {
      timestamp = extractTimestampFromFrame(data, 1, width);
    } else {
      timestamp = 0;
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

    if (timestamp !== 0) {
      const tsString = convertTimestampToString(timestamp);
      // Set the text properties
      const textHeight = 20;
      const padding = 4;
      ctx.font = `${textHeight}px Arial`;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      const x = canvas.width / 4;
      const y = canvas.height / 4;
      // Measure the text width and height
      const textMetrics = ctx.measureText(tsString);
      const textWidth = textMetrics.width;

      // Adjust for the lack of descenders by moving the box up slightly
      const descenderAdjustment = textHeight * 0.1;

      // Draw a white rectangle behind the text
      ctx.fillStyle = '#ffffffa0';
      ctx.fillRect(
        x - textWidth / 2 - padding,
        y - textHeight / 2 - padding,
        textWidth + padding * 2,
        textHeight + padding * 2,
      );

      ctx.strokeStyle = 'black';
      ctx.lineWidth = 2;
      ctx.strokeRect(
        x - textWidth / 2 - padding,
        y - textHeight / 2 - padding,
        textWidth + padding * 2,
        textHeight + padding * 2,
      );

      ctx.fillStyle = 'black';
      ctx.fillText(tsString, x, y + descenderAdjustment);
    }
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
