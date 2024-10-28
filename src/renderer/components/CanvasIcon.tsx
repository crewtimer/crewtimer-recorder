import React, { useEffect } from 'react';
import { SvgIconComponent } from '@mui/icons-material';
import ReactDOMServer from 'react-dom/server';

interface CanvasIconProps {
  icon: SvgIconComponent; // Icon component to render
  iconSize?: number; // Size of the icon in pixels
  color?: string; // Color of the icon
  setImage: (image: HTMLImageElement) => void; // Function to pass the Image object to the parent
}

const CanvasIcon: React.FC<CanvasIconProps> = ({
  icon: Icon,
  iconSize = 32,
  color = 'black',
  setImage,
}) => {
  useEffect(() => {
    // Create an offscreen canvas
    const offscreenCanvas = document.createElement('canvas');
    offscreenCanvas.width = iconSize;
    offscreenCanvas.height = iconSize;
    const context = offscreenCanvas.getContext('2d');

    if (context) {
      // Draw the icon to the offscreen canvas as an SVG
      const svg = `
        <svg xmlns="http://www.w3.org/2000/svg" width="${iconSize}" height="${iconSize}" fill="${color}">
          ${ReactDOMServer.renderToString(<Icon fontSize="inherit" />)}
        </svg>
      `;
      const img = new Image();
      img.src = `data:image/svg+xml;base64,${btoa(svg)}`;

      // Once the SVG image loads, draw it to the offscreen canvas and create a new Image object
      img.onload = () => {
        context.drawImage(img, 0, 0, iconSize, iconSize);

        // Create a new Image object from the canvas content
        const canvasImage = new Image();
        canvasImage.src = offscreenCanvas.toDataURL(); // Set the canvas data URL as the Image source

        // Pass the Image object to the parent via the setImage function
        setImage(canvasImage);
      };
    }
  }, [Icon, iconSize, color, setImage]);

  return null; // No visible component
};

export default CanvasIcon;
