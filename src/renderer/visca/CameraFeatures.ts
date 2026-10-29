import { focusFromDistance as aidaX30FocusFromDistance } from './cameras/aida_uhd_ndi3_x30_focus';

/** Subject distances, in meters, that Distance focus accepts. */
export const focusDistanceRange = { min: 10, max: 100 };

/** Features beyond the basics that only some camera models have. */
export interface CameraFeatures {
  /** The focus position that holds a subject this many meters away sharp at a zoom, or null where the model has no answer. */
  focusFromDistance?: (zoom: number, meters: number) => number | null;
}

/**
 * Camera IDs name where the ID came from, since cameras identify themselves differently (a
 * Basler camera has no VISCA). A VISCA camera's ID is the vendor and model ID from the
 * version inquiry.
 */
export const viscaCameraId = (model: string | undefined) =>
  model === undefined ? undefined : `visca:${model}`;

const featuresByCamera: Record<string, CameraFeatures> = {
  // AIDA UHD-NDI3-X30
  'visca:0005011f': { focusFromDistance: aidaX30FocusFromDistance },
};

export const cameraFeatures = (cameraId: string | undefined): CameraFeatures =>
  (cameraId !== undefined && featuresByCamera[cameraId]) || {};
