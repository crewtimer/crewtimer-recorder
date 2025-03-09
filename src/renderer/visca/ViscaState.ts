import { UseDatum } from 'react-usedatum';

export enum ExposureMode {
  EXPOSURE_AUTO = 0,
  EXPOSURE_MANUAL = 0x3,
  EXPOSURE_SHUTTER = 0xa,
  EXPOSURE_IRIS = 0xb,
  EXPOSURE_BRIGHT = 0xd,
}

// Interface describing the camera's state
export interface CameraState {
  autoFocus: boolean;
  exposureMode: ExposureMode;
  iris: number;
  shutter: number;
  gain: number;
  brightness: number;
  focus: number;
  zoom: number;
}

export const [useViscaIP, , getViscaIp] = UseDatum('10.0.1.188');
export const [useViscaPort, , getViscaPort] = UseDatum(52381);
export const [useCameraState, setCameraState, getCameraState] =
  UseDatum<CameraState>({
    autoFocus: false,
    exposureMode: ExposureMode.EXPOSURE_AUTO,
    iris: 0,
    shutter: 0,
    gain: 0,
    brightness: 0,
    focus: 0,
    zoom: 0,
  });
