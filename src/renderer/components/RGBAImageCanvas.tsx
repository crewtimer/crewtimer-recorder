/* eslint-disable no-bitwise */
import React, { useCallback, useEffect, useRef, useState } from 'react';
import { Box } from '@mui/material';
import EditIcon from '@mui/icons-material/Edit';
import CheckIcon from '@mui/icons-material/Check';
import FullscreenIcon from '@mui/icons-material/Fullscreen';
import VerticalAlignCenterIcon from '@mui/icons-material/VerticalAlignCenter';
import {
  getGuide,
  getRecordingProps,
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
import generateTestPattern, {
  drawBox,
  drawSvgIcon,
  translateDestCanvas2SrcCanvas,
  translateSrcCanvas2DestCanvas,
} from '../util/ImageUtils';
import {
  Point,
  getVideoScaling,
  setVideoScaling,
  useVideoScaling,
} from '../util/VideoSettings';
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

  number = (5000n + number) / 10000n; // Round 64-bit number to milliseconds

  return Number(number); // Convert the BigInt number to a regular number
}

interface CanvasProps {
  divwidth: number;
  divheight: number;
}
let lastGoodFrame: GrabFrameResponse = generateTestPattern();

// Given a clip Rect in percentage units, return the native px units equivalent
const getNativeClip = (imageClip: Rect): Rect => {
  const { srcWidth, srcHeight } = getVideoScaling();
  // Calculate the two corner points of the clip area in canvas px units
  const nativePt1 = translateSrcCanvas2DestCanvas({
    x: Math.round((imageClip.x * srcWidth) / 4) * 4,
    y: Math.round((imageClip.y * srcHeight) / 4) * 4,
  });
  const nativePt2 = translateSrcCanvas2DestCanvas({
    x: Math.round(((imageClip.x + imageClip.width) * srcWidth) / 4) * 4,
    y: Math.round(((imageClip.y + imageClip.height) * srcHeight) / 4) * 4,
  });
  // Convert the two points to a rectangle
  const nativeClip = {
    x: nativePt1.x,
    y: nativePt1.y,
    width: nativePt2.x - nativePt1.x,
    height: nativePt2.y - nativePt1.y,
  };
  return nativeClip;
};

/**
 * Get the coordinates of the guide in source image coordinates
 * @returns {pt1: Point, pt2: Point} in source image coordinates
 */
const getSrcGuideCoords = () => {
  const { cropArea } = getRecordingProps();
  const guide = getGuide();
  const { srcWidth, srcHeight } = getVideoScaling();
  const pt1: Point = {
    x: cropArea.x * srcWidth + (cropArea.width * srcWidth) / 2 + guide.pt1,
    y: cropArea.y * srcHeight,
  };
  const pt2: Point = {
    x: cropArea.x * srcWidth + (cropArea.width * srcWidth) / 2 + guide.pt2,
    y: (cropArea.y + cropArea.height) * srcHeight,
  };
  return { pt1, pt2 };
};

/**
 * Get the coordinates of the guide in destination canvas coordinates
 * @returns {pt1: Point, pt2: Point} in destination canvas coordinates
 */
const getNativeGuideCoords = () => {
  const guideSrcCoords = getSrcGuideCoords();
  const pt1 = translateSrcCanvas2DestCanvas({
    x: guideSrcCoords.pt1.x,
    y: guideSrcCoords.pt1.y,
  });
  const pt2 = translateSrcCanvas2DestCanvas({
    x: guideSrcCoords.pt2.x,
    y: guideSrcCoords.pt2.y,
  });
  return { pt1, pt2 };
};

const applyZoom = ({ srcPoint, zoom }: { srcPoint: Point; zoom: number }) => {
  const videoScaling = getVideoScaling();
  const destZoomWidth = videoScaling.destWidth * zoom;
  const destZoomHeight = videoScaling.destHeight * zoom;

  const { srcWidth, srcHeight } = videoScaling;

  let srcCenterPoint = srcPoint;
  if (srcPoint.x === 0 || zoom === 1) {
    srcCenterPoint = { x: srcWidth / 2, y: srcHeight / 2 };
  } else if (getRecordingProps().showFinishGuide) {
    const guideSrcCoords = getSrcGuideCoords();
    srcCenterPoint.x = (guideSrcCoords.pt1.x + guideSrcCoords.pt2.x) / 2; // Center around Finish Guide
  }

  // Calculate the aspect ratio
  const srcAspectRatio = srcWidth / srcHeight;
  const destAspectRatio = destZoomWidth / destZoomHeight;

  let scaledWidth: number;
  let scaledHeight: number;
  let pixScale: number;
  // Maintain the aspect ratio
  if (srcAspectRatio > destAspectRatio) {
    // Source is wider relative to destination
    scaledWidth = destZoomWidth;
    scaledHeight = destZoomWidth / srcAspectRatio;
    pixScale = srcHeight / scaledHeight;
  } else {
    // Source is taller relative to destination
    scaledWidth = destZoomHeight * srcAspectRatio;
    scaledHeight = destZoomHeight;
    pixScale = srcWidth / scaledWidth;
  }

  const destX =
    videoScaling.destWidth / 2 - scaledWidth * (srcCenterPoint.x / srcWidth);
  const destY = Math.min(
    0,
    videoScaling.destHeight / 2 - scaledHeight * (srcCenterPoint.y / srcHeight),
  );

  const pt1 = translateSrcCanvas2DestCanvas({ x: 0, y: 0 }, videoScaling);
  const pt2 = translateSrcCanvas2DestCanvas(
    { x: videoScaling.srcWidth, y: videoScaling.srcHeight },
    videoScaling,
  );

  // Specify the actual area in the canvas that will show video data
  const drawableRect: Rect = {
    x: Math.max(destX, 0),
    y: 0,
    width: videoScaling.destWidth - 2 * Math.max(destX, 0),
    height: Math.min(destY + scaledHeight, videoScaling.destHeight),
  };

  setVideoScaling((prior) => ({
    ...prior,
    destX,
    destY,
    srcCenterPoint,
    scaledWidth,
    scaledHeight,
    zoom,
    pixScale,
    drawableRect,
    pt1,
    pt2,
  }));
};

const isZooming = () => getVideoScaling().zoom !== 1;

const clearZoom = () => {
  applyZoom({ zoom: 1, srcPoint: getVideoScaling().srcCenterPoint });
};

const RGBAImageCanvas: React.FC<CanvasProps> = ({ divwidth, divheight }) => {
  let [frame] = useFrameGrab();
  const [guide, setGuide] = useGuide();
  const [isRecording] = useIsRecording();
  const [settings] = useRecordingProps();
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const [videoScaling] = useVideoScaling();
  // State for the selection rectangle
  const [recordingProps, setRecordingProps] = useRecordingProps();

  const [clip, setClip] = useState<Rect>(recordingProps.cropArea);
  const draggingCornerRef = useRef<string | null>(null);
  const [draggingCorner, setDraggingCorner] = useState<string | null>(null);
  const [isRectangleVisible, setIsRectangleVisible] = useState(false);
  const ignoreClick = useRef(false);
  const srcCenter = useRef<Point>({ x: divwidth / 2, y: divheight / 2 });
  const [editImage, setEditImage] = useState<HTMLImageElement | null>(null);
  const [checkImage, setCheckImage] = useState<HTMLImageElement | null>(null);
  const [fullscreenImage, setFullscreenImage] =
    useState<HTMLImageElement | null>(null);
  const [snapToCenterImage, setSnapToCenterImage] =
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

  useEffect(() => {
    srcCenter.current = { x: frame.width / 2, y: frame.height / 2 };
    setVideoScaling((prior) => ({
      ...prior,
      srcWidth: frame.width,
      srcHeight: frame.height,
      destWidth: divwidth,
      destHeight: divheight,
    }));
    applyZoom({
      zoom: 1,
      srcPoint: { x: frame.width / 2, y: frame.height / 2 },
    });
    // drawContentDebounced();
  }, [frame.width, frame.height, divwidth, divheight]);

  // Check if the point is within a rectangle corner
  const isInCorner = (x: number, y: number) => {
    const rect = getNativeClip(clip);
    const cornerSize = 10;
    const { pixScale, drawableRect } = getVideoScaling();
    const corners = [
      { name: 'tl', x: rect.x, y: rect.y },
      { name: 'tr', x: rect.x + rect.width, y: rect.y },
      { name: 'bl', x: rect.x, y: rect.y + rect.height },
      { name: 'br', x: rect.x + rect.width, y: rect.y + rect.height },
      {
        name: 'ft',
        x: guide.pt1 / pixScale + rect.x + rect.width / 2,
        y: Math.max(drawableRect.y, rect.y),
      },
      {
        name: 'fb',
        x: guide.pt2 / pixScale + rect.x + rect.width / 2,
        y: Math.min(drawableRect.y + drawableRect.height, rect.y + rect.height),
      },
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
      const guide1 = getSrcGuideCoords();
      setRecordingProps((prior) => ({ ...prior, cropArea: clip }));
      // Updte the guide position so it doesn't move when the crop changes
      const guide2 = getSrcGuideCoords();
      const dx = Math.round(guide1.pt1.x - guide2.pt1.x);
      setGuide((prevGuide) => {
        return {
          pt1: prevGuide.pt1 + dx,
          pt2: prevGuide.pt2 + dx,
        };
      });
      if (isRecording) {
        stopRecording().catch(showErrorDialog);
        setTimeout(() => startRecording().catch(showErrorDialog), 100);
      }
    } else {
      setClip(recordingProps.cropArea);
    }
    setIsRectangleVisible(!isRectangleVisible);
  };

  const isInIcon = (
    x: number,
    y: number,
    iconX: number,
    iconY: number,
    iconSize = 24,
  ) => {
    return (
      x >= iconX && x <= iconX + iconSize && y >= iconY && y <= iconY + iconSize
    );
  };

  const handleMouseDown = (e: React.MouseEvent) => {
    if (!canvasRef.current) return;
    const { offsetX, offsetY } = e.nativeEvent;

    const iconSize = 24;
    const iconPadding = 10;

    // Icon positions relative to the canvas width
    const { drawableRect } = videoScaling;
    const toggleIconX =
      drawableRect.x + drawableRect.width - 2 * (iconSize + iconPadding);
    const maxSizeIconX =
      drawableRect.x + drawableRect.width - (iconSize + iconPadding);
    if (e.button === 2) {
      e.preventDefault();
      e.stopPropagation();
      // zoom in/out
      if (isZooming()) {
        clearZoom();
      } else {
        // const { cropArea } = getRecordingProps();
        // const { srcWidth, srcHeight, pixScale } = getVideoScaling();
        // const newZoom = Math.min(
        //   divwidth / (cropArea.width * srcWidth),
        //   divheight / (cropArea.height * srcHeight),
        // );
        // const center = translateDestCanvas2SrcCanvas({
        //   x: offsetX,
        //   y: offsetY,
        // });

        // center.y = (cropArea.y + cropArea.height / 2) * srcHeight;
        // center.x = (cropArea.x + cropArea.width / 2) * srcWidth;
        // applyZoom({
        //   srcPoint: center,
        //   zoom: pixScale * newZoom,
        // });
        applyZoom({
          srcPoint: translateDestCanvas2SrcCanvas({ x: offsetX, y: offsetY }),
          zoom: 4,
        });
      }
    } else if (isInIcon(offsetX, offsetY, toggleIconX, iconPadding)) {
      ignoreClick.current = true;
      e.stopPropagation();
      handleEditIconClick();
    } else if (isInIcon(offsetX, offsetY, maxSizeIconX, iconPadding)) {
      ignoreClick.current = true;
      e.stopPropagation();
      handleMaximizeIconClick();
    } else if (isInIcon(offsetX, offsetY, maxSizeIconX, iconPadding * 2 + 24)) {
      ignoreClick.current = true;
      e.stopPropagation();
      setGuide({
        pt1: 0,
        pt2: 0,
      });
    } else if (isRectangleVisible) {
      const corner = isInCorner(offsetX, offsetY);
      if (corner) {
        ignoreClick.current = true;
        e.stopPropagation();
        setDraggingCorner(corner.name);
      }
    }
  };

  const handleMouseMove = useCallback(
    (e: MouseEvent) => {
      if (!draggingCornerRef.current || !canvasRef.current) return;

      const componentRect = canvasRef.current.getBoundingClientRect();
      let offsetX = Math.max(
        0,
        Math.min(componentRect.width, e.clientX - componentRect.left),
      );
      let offsetY = Math.max(
        0,
        Math.min(componentRect.height, e.clientY - componentRect.top),
      );
      const mouseOffsetX = offsetX;

      if (
        draggingCornerRef.current === 'ft' ||
        draggingCornerRef.current === 'fb'
      ) {
        const rect = getNativeClip(clip);
        const finishX =
          (mouseOffsetX - rect.x - rect.width / 2) * videoScaling.pixScale;

        setGuide((prevGuide) => {
          const delta = Math.round(finishX - prevGuide.pt1);
          return {
            pt1: draggingCornerRef.current === 'fb' ? prevGuide.pt1 : finishX,
            pt2:
              draggingCornerRef.current === 'ft'
                ? prevGuide.pt2 + delta
                : finishX,
          };
        });
      } else {
        offsetX -= videoScaling.destX;
        offsetY -= videoScaling.destY;
        offsetX /= videoScaling.scaledWidth;
        offsetY /= videoScaling.scaledHeight;
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
      }
    },
    [clip, setGuide, videoScaling],
  );

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
      videoScaling.srcWidth,
      videoScaling.srcHeight,
      videoScaling.destX,
      videoScaling.destY,
      videoScaling.scaledWidth,
      videoScaling.scaledHeight,
    );

    const destClipRect = getNativeClip(clip);
    if (clip.width !== 1 || clip.height !== 1) {
      ctx.save();
      ctx.beginPath();
      ctx.rect(
        videoScaling.destX,
        videoScaling.destY,
        videoScaling.scaledWidth,
        videoScaling.scaledHeight,
      );
      ctx.moveTo(destClipRect.x, destClipRect.y); // Move to top-left corner of the clear area
      ctx.rect(
        destClipRect.x,
        destClipRect.y,
        destClipRect.width,
        destClipRect.height,
      ); // Inner rectangle to exclude
      ctx.closePath();
      ctx.fillStyle = 'rgba(0, 0, 0, 0.5)';
      ctx.fill('evenodd'); // Fills everything except the inner rectangle

      // Restore the canvas state
      ctx.restore();
    }

    if (settings.showFinishGuide) {
      const nativeGuide = getNativeGuideCoords();
      const from = nativeGuide.pt1;
      const to = nativeGuide.pt2;
      from.y = Math.max(from.y, videoScaling.drawableRect.y);
      to.y = Math.min(
        to.y,
        videoScaling.drawableRect.y + videoScaling.drawableRect.height,
      );
      ctx.beginPath();
      ctx.moveTo(from.x, from.y);
      ctx.lineTo(to.x, to.y);
      ctx.strokeStyle = 'red';
      ctx.lineWidth = 1;
      ctx.stroke();
      if (isRectangleVisible) {
        drawBox(ctx, from.x, from.y, 12, 't');
        drawBox(ctx, to.x, to.y, 12, 'b');
      }
    }

    if (timestamp !== 0) {
      const tsString = convertTimestampToString(timestamp);
      // Set the text properties
      const textHeight = 20;
      const padding = 4;
      ctx.font = `${textHeight}px Arial`;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      const x = Math.max(videoScaling.destX, 0) + 8;
      const y = 36;
      // Measure the text width and height
      const textMetrics = ctx.measureText(tsString);
      const textWidth = textMetrics.width;
      const boxHeight = textHeight + padding * 2;

      // Draw a white rectangle behind the text
      ctx.fillStyle = '#ffffffa0';
      ctx.fillRect(x, y, textWidth + padding * 2, boxHeight);

      ctx.strokeStyle = 'black';
      ctx.lineWidth = 2;
      ctx.strokeRect(x, y, textWidth + padding * 2, boxHeight);

      ctx.fillStyle = 'black';
      ctx.fillText(tsString, x + padding + textWidth / 2, y + boxHeight / 2);
    }

    // Draw icons based on canvas width
    const iconSize = 24;
    const iconPadding = 10;
    const { drawableRect } = videoScaling;
    const toggleIconX =
      drawableRect.x + drawableRect.width - 2 * (iconSize + iconPadding);
    const maxSizeIconX =
      drawableRect.x + drawableRect.width - (iconSize + iconPadding);

    drawSvgIcon(
      ctx,
      isRectangleVisible ? checkImage : editImage,
      toggleIconX,
      iconPadding,
    );

    drawSvgIcon(ctx, fullscreenImage, maxSizeIconX, iconPadding);
    drawSvgIcon(
      ctx,
      snapToCenterImage,
      maxSizeIconX,
      iconPadding * 2 + 24,
      true,
    );

    // Draw the WxH text in the upper-left corner of the rectangle with a background
    const text = `${Math.round((clip.width * frame.width) / 4) * 4}x${Math.round((clip.height * frame.height) / 4) * 4}`;
    ctx.font = '16px Arial';
    ctx.textBaseline = 'middle';

    const textMetrics = ctx.measureText(text);
    const padding = 4;
    const textWidth = textMetrics.width + padding * 2;
    const textHeight = 16 + padding * 2;
    const edgeOffset = 8;

    // Draw background rectangle for the text
    ctx.fillStyle = 'rgba(50, 50, 50, 0.7)'; // Gray background with some transparency
    ctx.fillRect(
      edgeOffset + destClipRect.x,
      destClipRect.y + edgeOffset,
      textWidth,
      textHeight,
    );

    // Draw the text on top of the background
    ctx.fillStyle = 'white';
    ctx.textAlign = 'left';
    ctx.fillText(
      text,
      edgeOffset + destClipRect.x + padding,
      destClipRect.y + edgeOffset + textHeight / 2,
    ); // Adjust for padding and text baseline

    if (isRectangleVisible) {
      // Draw selection rectangle
      ctx.strokeStyle = '#ffffffb0';
      ctx.lineWidth = 3;
      ctx.strokeRect(
        destClipRect.x + 2,
        destClipRect.y + 2,
        destClipRect.width - 3,
        destClipRect.height - 3,
      );
      ctx.strokeStyle = '#ff0000b0';
      ctx.lineWidth = 1;
      ctx.strokeRect(
        destClipRect.x + 2,
        destClipRect.y + 2,
        destClipRect.width - 3,
        destClipRect.height - 3,
      );

      // Draw the corner handles
      drawBox(ctx, destClipRect.x, destClipRect.y, 12, 'tl');
      drawBox(
        ctx,
        destClipRect.x + destClipRect.width,
        destClipRect.y,
        12,
        'tr',
      );

      drawBox(
        ctx,
        destClipRect.x,
        destClipRect.y + destClipRect.height,
        12,
        'bl',
      );
      drawBox(
        ctx,
        destClipRect.x + destClipRect.width,
        destClipRect.y + destClipRect.height,
        12,
        'br',
      );
    } else {
      // Draw selection rectangle
      ctx.strokeStyle = '#ffffffb0';
      ctx.lineWidth = 2;
      ctx.strokeRect(
        destClipRect.x,
        destClipRect.y,
        destClipRect.width,
        destClipRect.height,
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
    editImage,
    checkImage,
    fullscreenImage,
    snapToCenterImage,
    videoScaling,
  ]);

  return (
    <Box
      sx={{ width: divwidth, height: divheight }}
      onMouseDown={handleMouseDown}
      onMouseUp={handleMouseUp}
      onContextMenu={(event) => {
        event.preventDefault();
      }}
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
      <CanvasIcon
        icon={VerticalAlignCenterIcon}
        iconSize={24}
        color="white"
        setImage={setSnapToCenterImage}
      />

      <canvas ref={canvasRef} style={{ width: '100%', height: '100%' }} />
    </Box>
  );
};

export default RGBAImageCanvas;
