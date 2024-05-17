import { Buffer } from 'buffer';
import { GrabFrameResponse } from '../recorder/RecorderTypes';

/**
 * Generate a 320x240 RGBA checkerboard pattern.
 * Each cell in the checkerboard will be 20x20 pixels.
 * Alternates between red and blue squares.
 *
 * @returns Buffer containing the image data.
 */
function generateTestPattern(): GrabFrameResponse {
  const width = 1280;
  const height = 720;
  const cellSize = 20; // Size of each square in the checkerboard pattern

  // Create a buffer to hold RGBA data for each pixel
  const totalPixels = width * height;
  const buffer = Buffer.alloc(totalPixels * 4); // 4 bytes per pixel

  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      // Determine which color to use based on the current cell
      const isRed =
        Math.floor(y / cellSize) % 2 === Math.floor(x / cellSize) % 2;
      const offset = (y * width + x) * 4;

      if (isRed) {
        buffer[offset] = 255; // Red channel
        buffer[offset + 1] = 255; // Green channel
        buffer[offset + 2] = 255; // Blue channel
        buffer[offset + 3] = 255; // Alpha channel (fully opaque)
      } else {
        // Blue square
        buffer[offset] = 0; // Red channel
        buffer[offset + 1] = 0; // Green channel
        buffer[offset + 2] = 0; // Blue channel
        buffer[offset + 3] = 128; // Alpha channel (fully opaque)
      }
    }
  }

  return {
    status: 'OK',
    data: buffer,
    width,
    height,
    totalBytes: totalPixels * 4,
  };
}

export default generateTestPattern;
