/* eslint-disable no-bitwise */
import React, { useCallback, useEffect, useRef, useState } from 'react';
import { Box } from '@mui/material';
import EditIcon from '@mui/icons-material/Edit';
import CheckIcon from '@mui/icons-material/Check';
import FullscreenIcon from '@mui/icons-material/Fullscreen';
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
import { GrabFrameResponse, Rect } from '../recorder/RecorderTypes';
import { showErrorDialog } from './ErrorDialog';
import CanvasIcon from './CanvasIcon';

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

const drawSvgIcon = (
  ctx: CanvasRenderingContext2D,
  image: HTMLImageElement | null,
  x: number,
  y: number,
) => {
  if (image) {
    ctx.fillStyle = 'rgba(50, 50, 50, 0.7)'; // '#99999980';
    ctx.fillRect(x, y, image.width, image.height);
    ctx.drawImage(image, x, y, image.width, image.height);
  }
};

const drawPlayIcon = (
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

  // State for the selection rectangle
  const [recordingProps, setRecordingProps] = useRecordingProps();

  const [clip, setClip] = useState<Rect>(recordingProps.cropArea);
  const draggingCornerRef = useRef<string | null>(null);
  const [draggingCorner, setDraggingCorner] = useState<string | null>(null);
  const [isRectangleVisible, setIsRectangleVisible] = useState(false);
  const ignoreClick = useRef(false);
  const scaleFactors = useRef({
    offsetX: 0,
    offsetY: 0,
    scaledWidth: 1280,
    scaledHeight: 720,
    scale: 1,
  });
  const [editImage, setEditImage] = useState<HTMLImageElement | null>(null);
  const [checkImage, setCheckImage] = useState<HTMLImageElement | null>(null);
  const [fullscreenImage, setFullscreenImage] =
    useState<HTMLImageElement | null>(null);

  useEffect(() => {
    draggingCornerRef.current = draggingCorner;
  }, [draggingCorner]);

  if (frame?.data) {
    lastGoodFrame = frame;
  } else {
    frame = lastGoodFrame;
  }

  useEffect(() => {
    let crop = { ...recordingProps.cropArea };
    if (crop.width === 0 || crop.height === 0) {
      crop = {
        x: 0,
        y: 0,
        width: 1,
        height: 1,
      };
    }
    setClip(crop);
  }, [recordingProps.cropArea]);

  // Given a clip Rect in percentage units, return the native px units equivalent
  const getNativeClip = useCallback((imageClip: Rect): Rect => {
    const nativeClip = {
      x: imageClip.x * scaleFactors.current.scaledWidth,
      y: imageClip.y * scaleFactors.current.scaledHeight,
      width: imageClip.width * scaleFactors.current.scaledWidth,
      height: imageClip.height * scaleFactors.current.scaledHeight,
    };
    return nativeClip;
  }, []);

  const togglePlay = () => {
    if (ignoreClick.current) {
      ignoreClick.current = false;
      return;
    }
    if (isRecording) {
      stopRecording().catch(showErrorDialog);
    } else {
      if (isRectangleVisible) {
        setRecordingProps((prior) => ({ ...prior, cropArea: clip }));
        setIsRectangleVisible(false);
      }
      startRecording().catch(showErrorDialog);
    }
  };

  // Check if the point is within a rectangle corner
  const isInCorner = (x: number, y: number) => {
    const rect = getNativeClip(clip);
    const cornerSize = 10;
    const corners = [
      { name: 'tl', x: rect.x, y: rect.y },
      { name: 'tr', x: rect.x + rect.width, y: rect.y },
      { name: 'bl', x: rect.x, y: rect.y + rect.height },
      { name: 'br', x: rect.x + rect.width, y: rect.y + rect.height },
    ];
    return corners.find(
      (corner) =>
        x >= corner.x - cornerSize &&
        x <= corner.x + cornerSize &&
        y >= corner.y - cornerSize &&
        y <= corner.y + cornerSize,
    );
  };
  const handleMaximizeIconClick = () => {
    if (canvasRef.current) {
      const maxClip = {
        x: 0,
        y: 0,
        width: 1,
        height: 1,
      };
      setClip(maxClip);
      setRecordingProps((prior) => ({ ...prior, cropArea: maxClip }));
    }
  };

  const handleEditIconClick = () => {
    if (isRectangleVisible) {
      setRecordingProps((prior) => ({ ...prior, cropArea: clip }));
      if (isRecording) {
        stopRecording().catch(showErrorDialog);
        setTimeout(() => startRecording().catch(showErrorDialog), 100);
      }
    } else {
      setClip(recordingProps.cropArea);
    }
    setIsRectangleVisible(!isRectangleVisible);
  };

  const isInIcon = (x: number, y: number, iconX: number, iconY: number) => {
    const iconSize = 24;
    return (
      x >= iconX && x <= iconX + iconSize && y >= iconY && y <= iconY + iconSize
    );
  };

  const handleMouseDown = (e: React.MouseEvent) => {
    let { offsetX, offsetY } = e.nativeEvent;
    offsetX -= scaleFactors.current.offsetX;
    offsetY -= scaleFactors.current.offsetY;

    if (!canvasRef.current) return;

    const iconSize = 24;
    const iconPadding = 10;

    // Icon positions relative to the canvas width
    const toggleIconX =
      scaleFactors.current.scaledWidth - 2 * (iconSize + iconPadding);
    const maxSizeIconX =
      scaleFactors.current.scaledWidth - (iconSize + iconPadding);
    if (isInIcon(offsetX, offsetY, toggleIconX, iconPadding)) {
      ignoreClick.current = true;
      e.stopPropagation();
      handleEditIconClick();
    } else if (isInIcon(offsetX, offsetY, maxSizeIconX, iconPadding)) {
      ignoreClick.current = true;
      e.stopPropagation();
      handleMaximizeIconClick();
    } else if (isRectangleVisible) {
      const corner = isInCorner(offsetX, offsetY);
      if (corner) {
        ignoreClick.current = true;
        e.stopPropagation();
        setDraggingCorner(corner.name);
      }
    }
  };

  const handleMouseMove = useCallback((e: MouseEvent) => {
    if (!draggingCornerRef.current || !canvasRef.current) return;

    const rect = canvasRef.current.getBoundingClientRect();
    let offsetX = Math.max(0, Math.min(rect.width, e.clientX - rect.left));
    let offsetY = Math.max(0, Math.min(rect.height, e.clientY - rect.top));
    offsetX -= scaleFactors.current.offsetX;
    offsetY -= scaleFactors.current.offsetY;
    offsetX /= scaleFactors.current.scaledWidth;
    offsetY /= scaleFactors.current.scaledHeight;
    setClip((prevRect) => {
      const newRect = { ...prevRect };
      const clipValue = (v: number) => {
        return Math.min(1, Math.max(0, v));
      };
      switch (draggingCornerRef.current) {
        case 'tl':
          newRect.width += newRect.x - offsetX;
          newRect.height += newRect.y - offsetY;
          newRect.x = offsetX;
          newRect.y = offsetY;
          break;
        case 'tr':
          newRect.width = offsetX - newRect.x;
          newRect.height += newRect.y - offsetY;
          newRect.y = offsetY;
          break;
        case 'bl':
          newRect.width += newRect.x - offsetX;
          newRect.x = offsetX;
          newRect.height = offsetY - newRect.y;
          break;
        case 'br':
          newRect.width = offsetX - newRect.x;
          newRect.height = offsetY - newRect.y;
          break;
        default:
          break;
      }
      newRect.x = clipValue(newRect.x);
      newRect.y = clipValue(newRect.y);
      newRect.width = clipValue(newRect.width);
      newRect.height = clipValue(newRect.height);

      return newRect;
    });
  }, []);

  const handleMouseUp = () => setDraggingCorner(null);

  useEffect(() => {
    if (draggingCorner) {
      // Add mouseup listener to window
      window.addEventListener('mouseup', handleMouseUp);
      window.addEventListener('mousemove', handleMouseMove);

      // Cleanup the event listener
      return () => {
        window.removeEventListener('mouseup', handleMouseUp);
        window.removeEventListener('mousemove', handleMouseMove);
      };
    }
    return () => {};
  }, [draggingCorner, handleMouseMove]);

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
    scaleFactors.current.scale = Math.min(
      parentWidth / width,
      parentHeight / height,
    );

    scaleFactors.current.scaledWidth = width * scaleFactors.current.scale;
    scaleFactors.current.scaledHeight = height * scaleFactors.current.scale;

    // Center the image in the canvas
    scaleFactors.current.offsetX =
      (parentWidth - scaleFactors.current.scaledWidth) / 2;
    scaleFactors.current.offsetY = 0; // (parentHeight - scaleFactors.current.scaledHeight) / 2;
    const { scale, offsetX, offsetY, scaledWidth, scaledHeight } =
      scaleFactors.current;

    const rect = getNativeClip(clip);

    canvas.width = parentWidth;
    canvas.height = parentHeight;

    const clipWidth = Math.round(rect.width / scale);
    const clipHeight = Math.round(rect.height / scale);

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

    let centerX = offsetX + scaledWidth / 2;
    if (width !== clipWidth || height !== clipHeight) {
      // Apply grey dimming outside the rectangle
      ctx.fillStyle = 'rgba(0, 0, 0, 0.5)';
      ctx.fillRect(offsetX, offsetY, scaledWidth, scaledHeight);

      // Clear the area within the rectangle to make it non-dimmed
      ctx.clearRect(
        offsetX + rect.x,
        offsetY + rect.y,
        rect.width,
        rect.height,
      );

      // Redraw the area inside the rectangle to remove transparency
      ctx.drawImage(
        offscreenCanvas,
        rect.x / scale,
        rect.y / scale,
        rect.width / scale,
        rect.height / scale,
        offsetX + rect.x,
        offsetY + rect.y,
        rect.width,
        rect.height,
      );
      centerX = rect.x + rect.width / 2;
    }

    if (settings.showFinishGuide) {
      // Draw the red line
      ctx.beginPath();
      ctx.moveTo(offsetX + centerX + guide.pt1 * scale, offsetY + rect.y); // Horizontal center + N pixels offset, scaled
      ctx.lineTo(
        offsetX + centerX + guide.pt2 * scale,
        offsetY + rect.y + rect.height,
      );
      ctx.strokeStyle = 'red';
      ctx.lineWidth = 1; // Line width can be adjusted as needed
      ctx.stroke();
    }

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

    drawPlayIcon(ctx, parentWidth, scaledHeight, isRecording);

    // Draw icons based on canvas width
    const iconSize = 24;
    const iconPadding = 10;
    const toggleIconX = offsetX + scaledWidth - 2 * (iconSize + iconPadding);
    const maxSizeIconX = offsetX + scaledWidth - (iconSize + iconPadding);

    drawSvgIcon(
      ctx,
      isRectangleVisible ? checkImage : editImage,
      toggleIconX,
      iconPadding,
    );

    drawSvgIcon(ctx, fullscreenImage, maxSizeIconX, iconPadding);

    if (isRectangleVisible) {
      // Draw selection rectangle
      ctx.strokeStyle = '#ffffffb0';
      ctx.lineWidth = 3;
      ctx.strokeRect(
        offsetX + rect.x + 2,
        offsetY + rect.y + 2,
        rect.width - 3,
        rect.height - 3,
      );
      ctx.strokeStyle = '#ff0000b0';
      ctx.lineWidth = 1;
      ctx.strokeRect(
        offsetX + rect.x + 2,
        offsetY + rect.y + 2,
        rect.width - 3,
        rect.height - 3,
      );

      // Draw the WxH text in the upper-left corner of the rectangle with a background
      const text = `${Math.round((clip.width * frame.width) / 4) * 4}x${Math.round((clip.height * frame.height) / 4) * 4}`;
      ctx.font = '16px Arial';
      ctx.textBaseline = 'middle';

      const textMetrics = ctx.measureText(text);
      const padding = 4;
      const textWidth = textMetrics.width + padding * 2;
      const textHeight = 16 + padding * 2;

      // Draw background rectangle for the text
      ctx.fillStyle = 'rgba(50, 50, 50, 0.7)'; // Gray background with some transparency
      ctx.fillRect(offsetX + rect.x, offsetY + rect.y, textWidth, textHeight);

      // Draw the text on top of the background
      ctx.fillStyle = 'white';
      ctx.textAlign = 'left';
      ctx.fillText(text, offsetX + rect.x + padding, offsetY + rect.y + 16); // Adjust for padding and text baseline

      // Draw corner handles
      ctx.fillStyle = '#ff0000b0';
      const corners = [
        { x: rect.x + 3, y: rect.y + 3 },
        { x: rect.x - 3 + rect.width, y: rect.y + 3 },
        { x: rect.x + 3, y: rect.y + rect.height - 3 },
        { x: rect.x + rect.width - 3, y: rect.y + rect.height - 3 },
      ];
      corners.forEach(({ x, y }) => {
        ctx.fillRect(offsetX + x - 5, offsetY + y - 5, 10, 10);
      });
    } else {
      // Draw selection rectangle
      ctx.strokeStyle = '#ffffffb0';
      ctx.lineWidth = 2;
      ctx.strokeRect(
        offsetX + rect.x,
        offsetY + rect.y,
        rect.width,
        rect.height,
      );
    }
  }, [
    isRectangleVisible,
    frame,
    clip,
    divwidth,
    divheight,
    guide,
    isRecording,
    settings.showFinishGuide,
    getNativeClip,
    editImage,
    checkImage,
    fullscreenImage,
  ]);

  return (
    <Box
      sx={{ width: divwidth, height: divheight }}
      onMouseDown={handleMouseDown}
      onMouseUp={handleMouseUp}
      onClick={togglePlay}
    >
      <CanvasIcon
        icon={EditIcon}
        iconSize={24}
        color="white"
        setImage={setEditImage}
      />
      <CanvasIcon
        icon={CheckIcon}
        iconSize={24}
        color="white"
        setImage={setCheckImage}
      />
      <CanvasIcon
        icon={FullscreenIcon}
        iconSize={24}
        color="white"
        setImage={setFullscreenImage}
      />

      <canvas ref={canvasRef} style={{ width: '100%', height: '100%' }} />
    </Box>
  );
};

export default RGBAImageCanvas;
