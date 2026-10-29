import { UseDatum } from 'react-usedatum';
import { UseStoredDatum } from '../store/UseElectronDatum';
import { Rect } from '../recorder/RecorderTypes';

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

/** Vendor and model ID from the VISCA version inquiry; the learned lens data belongs to this model. */
export const [useCameraModel, setCameraModel, getCameraModel] = UseStoredDatum<
  string | undefined
>('cameraModel', undefined);

/** The subject distance in meters that Distance focus keeps sharp as the zoom changes. */
export const [useFocusDistance] = UseStoredDatum('focusDistance', 30);
/** The VISCA address and port ("ip:port") of a camera, which binds per-camera state to it. */
export const viscaEndpoint = (ip: string, port: number) => `${ip}:${port}`;
/**
 * The model the camera at an endpoint reported this session. Unlike the stored model, it can't
 * describe a camera that hasn't answered yet.
 */
export const [useViscaModel, setViscaModel] = UseDatum<
  { endpoint: string; model: string } | undefined
>(undefined);
/**
 * The endpoint of the camera whose focus follows the zoom to hold the focus distance, or
 * undefined when Distance focus is off; another camera never inherits it.
 */
export const [useDistanceFocusCamera, setDistanceFocusCamera] = UseDatum<
  string | undefined
>(undefined);

export interface LensRange {
  zoom: { min: number; max: number };
  focus: { min: number; max: number };
}

/** The lowest and highest zoom and focus positions seen so far; it widens as the lens is used. */
export const [useLensRange, setLensRange, getLensRange] = UseStoredDatum<
  LensRange | undefined
>('lensRange', undefined);

/** Sony VISCA position scales (zoom includes digital zoom); the X30 uses the zoom scale exactly. */
export const viscaScale: LensRange = {
  zoom: { min: 0, max: 0x7ac0 },
  focus: { min: 0x1000, max: 0xf000 },
};

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
/** Camera settings plus the crop region and timing line; presets saved before crop was added lack them. */
export interface CameraPreset extends CameraState {
  cropArea?: Rect;
  guide?: { pt1: number; pt2: number };
}
export const [useCameraPresets, setCameraPresets, getCameraPresets] =
  UseStoredDatum<CameraPreset[]>('presets', []);
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
