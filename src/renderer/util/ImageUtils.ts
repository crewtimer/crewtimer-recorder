import { Buffer } from 'buffer';
import { GrabFrameResponse } from '../recorder/RecorderTypes';
import { Point, getVideoScaling } from './VideoSettings';

function generateTestPattern(): GrabFrameResponse {
  const width = 1920;
  const height = 1080;
  const cellSize = 80;

  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  const ctx = canvas.getContext('2d')!;

  for (let row = 0; row * cellSize < height; row += 1) {
    for (let col = 0; col * cellSize < width; col += 1) {
      ctx.fillStyle = row % 2 === col % 2 ? '#ffffff' : '#c8c8c8';
      ctx.fillRect(col * cellSize, row * cellSize, cellSize, cellSize);
    }
  }

  const dataUrl = canvas.toDataURL('image/jpeg', 0.85);
  const base64 = dataUrl.split(',')[1];
  const data = Buffer.from(base64, 'base64');

  return {
    status: 'OK',
    data,
    width,
    height,
    focus: 1.0,
    totalBytes: data.length,
    tsMilli: Date.now(),
  };
}

export const drawText = (
  context: CanvasRenderingContext2D,
  x: number,
  y: number,
  text: string,
) => {
  const textHeight = 16;
  const padding = 4;
  context.font = `${textHeight}px Arial`; // set before measureText
  context.textAlign = 'center';
  context.textBaseline = 'middle';
  const textMetrics = context.measureText(text);
  const textWidth = textMetrics.width;
  const boxHeight = textHeight + padding * 2;
  const boxWidth = textWidth + padding * 2;

  // Draw a white rectangle behind the text
  context.fillStyle = 'rgba(50, 50, 50, 0.7)'; // Gray background with some transparency

  context.fillRect(x, y, boxWidth, boxHeight);

  context.strokeStyle = '#888888';
  context.lineWidth = 2;
  context.strokeRect(x, y, boxWidth, boxHeight);

  context.fillStyle = '#eeeeee';
  context.fillText(
    text,
    x + padding + textWidth / 2,
    y +
      boxHeight / 2 +
      (textMetrics.actualBoundingBoxAscent -
        textMetrics.actualBoundingBoxDescent) /
        2,
  );
};
export const drawBox = (
  context: CanvasRenderingContext2D,
  xpos: number,
  ypos: number,
  size: number,
  location: 'c' | 't' | 'b' | 'l' | 'r' | 'tl' | 'tr' | 'bl' | 'br',
  fill = true,
) => {
  let x = xpos;
  let y = ypos;
  switch (location) {
    case 'c':
      x -= size / 2;
      y -= size / 2;
      break;
    case 't':
      x -= size / 2;
      break;
    case 'b':
      x -= size / 2;
      y -= size;
      break;
    case 'l':
      y -= size / 2;
      break;
    case 'r':
      x -= size;
      y -= size / 2;
      break;
    case 'tl':
      break;
    case 'tr':
      x -= size;
      break;
    case 'bl':
      y -= size;
      break;
    case 'br':
      x -= size;
      y -= size;
      break;
    default:
      x -= size / 2;
      y -= size / 2;
      break;
  }
  context.save();
  context.strokeStyle = 'black';
  context.lineWidth = 1;
  context.strokeRect(x, y, size, size);
  if (fill) {
    context.fillStyle = 'white';
    context.fillRect(x + 1, y + 1, size - 2, size - 2);
    context.stroke();
  } else {
    context.strokeStyle = 'white';
    context.lineWidth = 1;
    context.strokeRect(x + 1, y + 1, size - 2, size - 2);
  }
  context.restore();
};

export const drawSvgIcon = (
  ctx: CanvasRenderingContext2D,
  image: HTMLImageElement | null,
  x: number,
  y: number,
  rotate90: boolean = false, // New property to control rotation
) => {
  if (image) {
    ctx.save(); // Save the current context state

    if (rotate90) {
      // Translate and rotate the context around the top-left corner of the image
      ctx.translate(x + image.height / 2, y + image.width / 2);
      ctx.rotate((90 * Math.PI) / 180); // Convert degrees to radians
      ctx.translate(-image.height / 2, -image.width / 2);
    } else {
      ctx.translate(x, y); // Translate to x, y for non-rotated image
    }

    ctx.fillStyle = 'rgba(50, 50, 50, 0.7)'; // '#99999980';
    ctx.fillRect(0, 0, image.width, image.height);
    ctx.drawImage(image, 0, 0, image.width, image.height);

    ctx.restore(); // Restore the context to its original state
  }
};

export const translateSrcCanvas2DestCanvas = (srcPoint: Point): Point => {
  const { destX, destY, pixScale } = getVideoScaling();
  return { x: destX + srcPoint.x * pixScale, y: destY + srcPoint.y * pixScale };
};

export const translateDestCanvas2SrcCanvas = (srcPoint: Point): Point => {
  const { destX, destY, pixScale } = getVideoScaling();

  return {
    x: (srcPoint.x - destX) / pixScale,
    y: (srcPoint.y - destY) / pixScale,
  };
};

export default generateTestPattern;
