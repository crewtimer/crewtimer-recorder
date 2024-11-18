import { UseDatum } from 'react-usedatum';
import { Rect } from '../recorder/RecorderTypes';

export type Point = { x: number; y: number };

export interface VideoScaling {
  destX: number; /// X offset in dest canvas units of image
  destY: number; /// Y offset in dest canvas units of image
  destWidth: number; /// Width in pixels of destination canvas
  destHeight: number; /// width in pixels of destination canvas
  srcCenterPoint: Point; /// Center point in source image units
  srcWidth: number; /// Width of source image
  srcHeight: number; /// Height of source image
  scaledWidth: number; /// Width in zoomed pixels of dest canvas
  scaledHeight: number; /// Height in zoomed pixels of dest canvas
  pixScale: number; /// convert canvas pixels to src pixels
  zoom: number; /// zoom factor
  drawableRect: Rect; // Native units where we will be drawing images
}

export const [useVideoScaling, setVideoScaling, getVideoScaling] =
  UseDatum<VideoScaling>({
    destX: 0,
    destY: 0,
    destWidth: 1,
    destHeight: 1,
    srcCenterPoint: { x: 0, y: 0 },
    srcWidth: 1,
    srcHeight: 1,
    scaledWidth: 1,
    scaledHeight: 1,
    pixScale: 1,
    zoom: 1,
    drawableRect: { x: 0, y: 0, width: 1, height: 1 },
  });
