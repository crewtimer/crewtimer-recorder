/* eslint-disable no-bitwise */
import React, { useCallback, useEffect, useRef, useState } from 'react';
import { Box, Typography } from '@mui/material';
import CropIcon from '@mui/icons-material/Crop';
import FullscreenIcon from '@mui/icons-material/Fullscreen';
import VerticalAlignCenterIcon from '@mui/icons-material/VerticalAlignCenter';
import {
  getGuide,
  getIsRecording,
  getRecordingProps,
  useFrameGrab,
  useGuide,
  useIsRecording,
  useRecordingProps,
  useRecordingPropsPending,
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
import retriggerableOneShot from './RetriggerableOneshot';

const DefaultCrop: Rect = {
  x: 0,
  y: 0,
  width: 1,
  height: 1,
};

const isDefaultCrop = (r: Rect) => {
  return (
    r.x === DefaultCrop.x &&
    r.y === DefaultCrop.y &&
    r.width === DefaultCrop.width &&
    r.height === DefaultCrop.height
  );
};

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

enum ZoomMode {
  Fit,
  Zoom,
  Maximize,
}

const applyZoom = ({
  center,
  zoomMode,
  cropRect: cropRectArg,
}: {
  center?: Point;
  zoomMode: ZoomMode;
  cropRect?: Rect;
}) => {
  const { srcWidth, srcHeight, destWidth, destHeight } = getVideoScaling();
  const baseScale = Math.min(destWidth / srcWidth, destHeight / srcHeight);
  const srcPoint = center || { x: srcWidth / 2, y: srcHeight / 2 };

  let pixScale = baseScale;
  let zoom = 1;
  let destX = 0;
  let destY = 0;

  switch (zoomMode) {
    case ZoomMode.Fit:
      pixScale = baseScale; // No zoom, center raw image
      destX = destWidth / 2 - (pixScale * srcWidth) / 2;
      destY = destHeight / 2 - (pixScale * srcHeight) / 2;
      break;
    case ZoomMode.Maximize:
      {
        // Center and maximize the crop region
        const cropRect = cropRectArg || getRecordingProps().cropArea;

        // Center of cropArea
        srcPoint.x = (cropRect.x + cropRect.width / 2) * srcWidth;
        srcPoint.y = (cropRect.y + cropRect.height / 2) * srcHeight;

        // Retain max scale while retaining aspect ratio
        const maxScale = Math.min(
          destWidth / (cropRect.width * srcWidth),
          destHeight / (cropRect.height * srcHeight),
        );

        // Compute scale and zoom
        pixScale = maxScale;
        zoom = pixScale / baseScale; // Addional zoom required to fill with crop region
        destX = destWidth / 2 - pixScale * srcPoint.x;
        destY = destHeight / 2 - pixScale * srcPoint.y;
      }
      break;
    case ZoomMode.Zoom:
      {
        // Zoom around a point of interest
        zoom = 6;
        pixScale = baseScale * zoom;

        const guideSrcCoords = getSrcGuideCoords();
        if (getRecordingProps().showFinishGuide) {
          srcPoint.x = (guideSrcCoords.pt1.x + guideSrcCoords.pt2.x) / 2; // Center around Finish Guide
        }

        // Normal centering around selected point
        destX = destWidth / 2 - pixScale * srcPoint.x;
        destY = destHeight / 2 - pixScale * srcPoint.y;

        // Adjust centering to show more of the crop area when near edges
        const { cropArea } = getRecordingProps();
        const yMin = -cropArea.y * srcHeight * pixScale;
        const yMax =
          -(cropArea.y + cropArea.height) * srcHeight * pixScale + destHeight;
        destY = Math.min(yMin, Math.max(yMax, destY));
        const xMin = -cropArea.x * srcWidth * pixScale;
        const xMax =
          -(cropArea.x + cropArea.width) * srcWidth * pixScale + destWidth;
        destX = Math.min(xMin, Math.max(xMax, destX));
      }
      break;
    default:
      break;
  }

  const scaledWidth = pixScale * srcWidth;
  const scaledHeight = pixScale * srcHeight;

  destY = Math.min(0, destY);

  // Specify the actual area in the canvas that will show video data
  const drawableRect: Rect = {
    x: Math.max(destX, 0),
    y: 0,
    width: destWidth - 2 * Math.max(destX, 0),
    height: Math.min(destY + scaledHeight, destHeight),
  };

  setVideoScaling((prior) => ({
    ...prior,
    destX,
    destY,
    srcCenterPoint: srcPoint,
    scaledWidth,
    scaledHeight,
    zoom,
    pixScale,
    drawableRect,
    zoomMode,
  }));
};

// const isZooming = () => getVideoScaling().zoom !== 1;

// const clearZoom = () => {
//   applyZoom({ zoomMode: ZoomMode.Fit });
// };

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
  const [isAdjustingCrop, setIsAdjustingCrop] = useState(false);
  const ignoreClick = useRef(false);
  const srcCenter = useRef<Point>({ x: divwidth / 2, y: divheight / 2 });
  const [cropImage, setEditImage] = useState<HTMLImageElement | null>(null);
  const [recordingPropsPending] = useRecordingPropsPending();
  const [fullscreenImage, setFullscreenImage] =
    useState<HTMLImageElement | null>(null);
  const [snapToCenterImage, setSnapToCenterImage] =
    useState<HTMLImageElement | null>(null);

  const [timeoutMessage, setTimeoutMessage] = useState('');

  const [applyChanges] = retriggerableOneShot((cropArea: Rect) => {
    const oldGuide = getSrcGuideCoords();
    setRecordingProps((prior) => ({ ...prior, cropArea }));
    // Updte the guide position so it doesn't move when the crop changes
    const newGuide = getSrcGuideCoords();
    const dx = Math.round(oldGuide.pt1.x - newGuide.pt1.x);

    setGuide((prevGuide) => {
      return {
        pt1: prevGuide.pt1 + dx,
        pt2: prevGuide.pt2 + dx,
      };
    });
    if (getIsRecording()) {
      stopRecording().catch(showErrorDialog);
      setTimeout(() => startRecording().catch(showErrorDialog), 100);
    }
  }, 2000);

  useEffect(() => {
    draggingCornerRef.current = draggingCorner;
  }, [draggingCorner]);
  useEffect(() => {
    setIsAdjustingCrop(clip.width !== 1 || clip.height !== 1);
  }, [clip, setIsAdjustingCrop]);

  if (frame?.data) {
    lastGoodFrame = frame;
  } else {
    frame = lastGoodFrame;
  }

  useEffect(() => {
    let crop = { ...recordingProps.cropArea };
    if (crop.width === 0 || crop.height === 0) {
      crop = { ...DefaultCrop };
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
      zoomMode: ZoomMode.Fit,
    });
    // drawContentDebounced();
  }, [frame.width, frame.height, divwidth, divheight]);

  useEffect(() => {
    if (!isRecording) {
      return () => {};
    }
    const timeout = setTimeout(() => {
      setTimeoutMessage('No Data Received!!');
    }, 1000);
    return () => {
      setTimeoutMessage('');
      clearTimeout(timeout);
    };
  }, [frame, isRecording]);

  // Check if the point is within a rectangle corner
  const isInCorner = (x: number, y: number) => {
    const cropRect = getNativeClip(clip);
    const cornerSize = 10;

    const nativeGuide = getNativeGuideCoords();
    const corners = [
      { name: 'tl', x: cropRect.x, y: cropRect.y },
      { name: 'tr', x: cropRect.x + cropRect.width, y: cropRect.y },
      { name: 'bl', x: cropRect.x, y: cropRect.y + cropRect.height },
      {
        name: 'br',
        x: cropRect.x + cropRect.width,
        y: cropRect.y + cropRect.height,
      },
      {
        name: 'ft',
        x: nativeGuide.pt1.x,
        y: cropRect.y,
      },
      {
        name: 'fb',
        x: nativeGuide.pt2.x,
        y: cropRect.y + cropRect.height,
      },
    ];

    const selectedCorner = corners.find(
      (corner) =>
        x >= corner.x - cornerSize &&
        x <= corner.x + cornerSize &&
        y >= corner.y - cornerSize &&
        y <= corner.y + cornerSize,
    );
    return selectedCorner;
  };

  const handleMaximizeIconClick = () => {
    if (canvasRef.current) {
      const maxClip = { ...DefaultCrop };
      if (isDefaultCrop(clip) && !isAdjustingCrop) {
        setIsAdjustingCrop(true);
      } else {
        setClip(maxClip);
        setRecordingProps((prior) => ({ ...prior, cropArea: maxClip }));
        setIsAdjustingCrop(!isAdjustingCrop);
        applyChanges(maxClip);
      }
    }
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
    const maxSizeIconX =
      drawableRect.x + drawableRect.width - (iconSize + iconPadding);
    if (e.button === 2) {
      e.preventDefault();
      e.stopPropagation();
      const center = translateDestCanvas2SrcCanvas({
        x: offsetX,
        y: offsetY,
      }); // mouse pos in src units
      let { zoomMode } = getVideoScaling();
      switch (zoomMode) {
        case ZoomMode.Fit:
          if (isDefaultCrop(clip)) {
            zoomMode = ZoomMode.Zoom;
          } else {
            zoomMode = ZoomMode.Maximize;
          }
          break;
        case ZoomMode.Maximize:
          zoomMode = ZoomMode.Zoom;
          break;
        case ZoomMode.Zoom:
        default:
          zoomMode = ZoomMode.Fit;
          break;
      }
      applyZoom({
        center,
        zoomMode,
        cropRect: clip,
      });
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
    } else {
      const corner = isInCorner(offsetX, offsetY);
      // Only enable dragging if we're adjusting crop already or it's the finish line that is selected
      if (corner && (isAdjustingCrop || corner?.name.startsWith('f'))) {
        ignoreClick.current = true;
        e.stopPropagation();
        setDraggingCorner(corner.name);
      }
    }
  };

  const handleMouseMove = useCallback(
    (e: MouseEvent) => {
      if (!draggingCornerRef.current || !canvasRef.current) return;
      const shift = e.shiftKey;

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
        const storedRect = getNativeClip(getRecordingProps().cropArea);

        // finish is always aligned on a pixel.
        const finishX = Math.round(
          (mouseOffsetX - storedRect.x - storedRect.width / 2) /
            videoScaling.pixScale,
        );

        setGuide((prevGuide) => {
          const delta = Math.round(finishX - prevGuide.pt1);
          if (shift) {
            return {
              pt1: draggingCornerRef.current === 'fb' ? prevGuide.pt1 : finishX,
              pt2: draggingCornerRef.current === 'fb' ? finishX : prevGuide.pt2,
            };
          }
          return { pt1: prevGuide.pt1 + delta, pt2: prevGuide.pt2 + delta };
        });
        applyChanges(clip);
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

          applyChanges(newRect);
          return newRect;
        });
      }
    },
    [clip, setGuide, videoScaling, applyChanges],
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

    const { data, width, height, tsMilli } = frame;
    const canvas = canvasRef.current;
    const ctx = canvas.getContext('2d');

    if (!ctx) {
      return;
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
      from.y = destClipRect.y;
      to.y = destClipRect.y + destClipRect.height;
      ctx.beginPath();
      ctx.moveTo(from.x, from.y);
      ctx.lineTo(to.x, to.y);
      ctx.strokeStyle = 'red';
      ctx.lineWidth = 1;
      ctx.stroke();
      drawBox(ctx, from.x, from.y, 12, 't');
      drawBox(ctx, to.x, to.y, 12, 'b');
    }

    if (tsMilli !== 0) {
      const tsString = convertTimestampToString(tsMilli);
      // Set the text properties
      const textHeight = 16;
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
      const boxWidth = textWidth + padding * 2;

      // Draw a white rectangle behind the text
      ctx.fillStyle = 'rgba(50, 50, 50, 0.7)'; // Gray background with some transparency

      ctx.fillRect(x, y, boxWidth, boxHeight);

      ctx.strokeStyle = '#888888';
      ctx.lineWidth = 2;
      ctx.strokeRect(x, y, boxWidth, boxHeight);

      ctx.fillStyle = '#eeeeee';
      ctx.fillText(
        tsString,
        x + padding + textWidth / 2,
        y +
          boxHeight / 2 +
          (textMetrics.actualBoundingBoxAscent -
            textMetrics.actualBoundingBoxDescent) /
            2,
      );
    }

    // Draw icons based on canvas width
    const iconSize = 24;
    const iconPadding = 10;
    const { drawableRect } = videoScaling;
    const maxSizeIconX =
      drawableRect.x + drawableRect.width - (iconSize + iconPadding);

    drawSvgIcon(
      ctx,
      isAdjustingCrop ? fullscreenImage : cropImage,
      maxSizeIconX,
      iconPadding,
    );
    drawSvgIcon(
      ctx,
      snapToCenterImage,
      maxSizeIconX,
      iconPadding * 2 + 24,
      true,
    );

    // Draw the WxH text in the upper-left corner of the rectangle with a background
    const text = `${Math.round((clip.width * frame.width) / 4) * 4}x${Math.round((clip.height * frame.height) / 4) * 4}`;
    const fontSize = 16;
    ctx.font = `${fontSize}px Arial`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';

    const textMetrics = ctx.measureText(text);
    const padding = 4;
    const boxWidth = textMetrics.width + padding * 2;
    const boxHeight = fontSize + padding * 2;
    const edgeOffset = 8;

    // Draw background rectangle for the text
    ctx.fillStyle = 'rgba(50, 50, 50, 0.7)'; // Gray background with some transparency
    ctx.fillRect(
      edgeOffset + destClipRect.x,
      edgeOffset + destClipRect.y,
      boxWidth,
      boxHeight,
    );

    ctx.strokeStyle = '#888888';
    ctx.lineWidth = 1;
    ctx.strokeRect(
      edgeOffset + destClipRect.x,
      edgeOffset + destClipRect.y,
      boxWidth,
      boxHeight,
    );

    // Draw the text on top of the background
    ctx.fillStyle = '#eeeeee';
    ctx.fillText(
      text,
      edgeOffset + destClipRect.x + padding + textMetrics.width / 2,
      edgeOffset +
        destClipRect.y +
        boxHeight / 2 +
        (textMetrics.actualBoundingBoxAscent -
          textMetrics.actualBoundingBoxDescent) /
          2,
    ); // Adjust for padding and text baseline

    if (isAdjustingCrop) {
      // Draw finish selection rectangle
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
    isAdjustingCrop,
    frame,
    clip,
    divwidth,
    divheight,
    guide,
    isRecording,
    settings.showFinishGuide,
    cropImage,
    fullscreenImage,
    snapToCenterImage,
    videoScaling,
  ]);

  let alertMessage = 'Video stopped. Press Start to resume.';
  if (isRecording) {
    if (timeoutMessage) {
      alertMessage = timeoutMessage;
    } else if (recordingPropsPending) {
      alertMessage =
        'Recording props have changes. Stop and Start recording to apply.';
    } else {
      alertMessage = '';
    }
  }

  return (
    <Box
      sx={{ width: divwidth, height: divheight, position: 'relative' }}
      onMouseDown={handleMouseDown}
      onMouseUp={handleMouseUp}
      onContextMenu={(event) => {
        event.preventDefault();
      }}
    >
      <CanvasIcon
        icon={CropIcon}
        iconSize={24}
        color="white"
        setImage={setEditImage}
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
      {alertMessage !== '' && (
        <Box
          position="absolute"
          top={0}
          left={(videoScaling.destWidth - videoScaling.scaledWidth) / 2}
          width={videoScaling.scaledWidth}
          height={videoScaling.scaledHeight}
          display="flex"
          justifyContent="center"
          alignItems="center"
          bgcolor="rgba(0, 0, 0, 0.4)"
          color="#fff"
          fontSize="1.5rem"
          zIndex={100}
        >
          <Typography sx={{ background: '#888a', padding: '0.5em' }}>
            {alertMessage}
          </Typography>
        </Box>
      )}
      <canvas ref={canvasRef} style={{ width: '100%', height: '100%' }} />
    </Box>
  );
};

export default RGBAImageCanvas;
