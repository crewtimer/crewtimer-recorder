export const hasCameraTimeMismatch = (cameraTime: number, systemTime: number) =>
  Number.isFinite(cameraTime) &&
  cameraTime > 0 &&
  Number.isFinite(systemTime) &&
  Math.abs(cameraTime - systemTime) > 2 * 60 * 1000;
