import { cameraFeatures, viscaCameraId } from './CameraFeatures';
import {
  distanceFromFocus,
  focusFromDistance,
} from './cameras/aida_uhd_ndi3_x30_focus';

describe('cameraFeatures', () => {
  it('offers distance focus on the AIDA UHD-NDI3-X30 only', () => {
    expect(
      cameraFeatures(viscaCameraId('0005011f')).focusFromDistance,
    ).toBeDefined();
    expect(
      cameraFeatures(viscaCameraId('00010519')).focusFromDistance,
    ).toBeUndefined();
    expect(
      cameraFeatures(viscaCameraId(undefined)).focusFromDistance,
    ).toBeUndefined();
    // A bare ID could come from any source, so it matches nothing.
    expect(cameraFeatures('0005011f').focusFromDistance).toBeUndefined();
  });
});

describe('AIDA UHD-NDI3-X30 focus model', () => {
  it('reproduces the fitted value at a calibration zoom', () => {
    // A + B / d at zoom 12743
    expect(focusFromDistance(12743, 50)).toBe(Math.round(13493 + 1050 / 50));
  });

  it('rejects distances and zooms outside the calibration', () => {
    expect(focusFromDistance(12743, 9)).toBeNull();
    expect(focusFromDistance(12743, 101)).toBeNull();
    expect(focusFromDistance(-1, 50)).toBeNull();
    // Digital zoom
    expect(focusFromDistance(16385, 50)).toBeNull();
  });

  it('interpolates between calibration zooms', () => {
    const below = focusFromDistance(13000, 30) as number;
    const above = focusFromDistance(13500, 30) as number;
    const between = focusFromDistance(13250, 30) as number;
    expect(between).toBeGreaterThanOrEqual(Math.min(below, above));
    expect(between).toBeLessThanOrEqual(Math.max(below, above));
  });

  it('inverts at telephoto', () => {
    const focus = focusFromDistance(16384, 40) as number;
    expect(distanceFromFocus(16384, focus)).toBeCloseTo(40, 0);
  });
});
