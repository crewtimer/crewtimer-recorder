import { UseDatum } from 'react-usedatum';

// Kept apart from MainPage so pages can navigate without an import cycle
export const [useSelectedPage] = UseDatum<string>('/');

export const SETTINGS_PAGES = ['/', '/home', '/index.html'];
