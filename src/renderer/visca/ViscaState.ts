import { UseDatum } from 'react-usedatum';
import { UseStoredDatum } from '../store/UseElectronDatum';

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

export const [useViscaIP, setViscaIP, getViscaIP] = UseStoredDatum(
  'viscaIP',
  '192.168.1.188',
);
export const [useViscaPort, , getViscaPort] = UseStoredDatum(
  'viscaPort',
  52381,
);
export const [useViscaState, setViscaState, getViscastate] = UseDatum('Idle');

/** End-of-travel positions found by sweeping the lens. Focus is measured at max zoom. */
export interface LensRange {
  zoom: { min: number; max: number };
  focus: { min: number; max: number };
}
export const [useLensRange, setLensRange, getLensRange] = UseStoredDatum<
  LensRange | undefined
>('lensRange', undefined);

/**
 * Focus ends the lens stopped at within one zoom band, and the zoom each was found at; focus
 * travel narrows with zoom, so an end is only certain at that zoom.
 */
export interface FocusReach {
  low?: number;
  high?: number;
  lowZoom?: number;
  highZoom?: number;
}
export const [useFocusReach, setFocusReach, getFocusReach] = UseStoredDatum<
  Record<number, FocusReach>
>('focusReach', {});

/** Which of 16 equal slices of the zoom range a zoom position falls in. */
export const zoomBand = (zoom: number, { min, max }: LensRange['zoom']) =>
  Math.min(15, Math.max(0, Math.floor(((zoom - min) / (max - min)) * 16)));
export const [useCameraPresets, setCameraPresets, getCameraPresets] =
  UseStoredDatum<CameraState[]>('presets', []);
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
