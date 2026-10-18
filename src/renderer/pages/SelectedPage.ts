import { UseDatum } from 'react-usedatum';

// Kept apart from MainPage so pages can navigate without an import cycle
// Start on Source so the camera can be picked or checked first
export const [useSelectedPage] = UseDatum<string>('/source');

/** Pages showing the recorder (preview plus side panel); '/' is the Camera tab. */
export const SETTINGS_PAGES = [
  '/',
  '/home',
  '/index.html',
  '/recording',
  '/source',
];
