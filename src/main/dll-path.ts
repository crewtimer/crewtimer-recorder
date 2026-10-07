import path from 'path';

// Windows resolves the native module's NDI DLL via the working directory and PATH,
// not the exe folder where it is installed. Must be imported before the native module.
if (process.platform === 'win32') {
  process.env.PATH = `${path.dirname(process.execPath)};${process.env.PATH}`;
}
