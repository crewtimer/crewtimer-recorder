import { UseDatum } from 'react-usedatum';

// Kept apart from MainPage so pages can navigate without an import cycle
export const [useSelectedPage] = UseDatum<string>('/');

/** Pages showing the recorder (preview plus side panel); '/' is the Camera tab. */
export const SETTINGS_PAGES = [
  '/',
  '/home',
  '/index.html',
  '/recording',
  '/source',
];
