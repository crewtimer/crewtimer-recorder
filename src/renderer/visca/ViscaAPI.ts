/* eslint-disable no-await-in-loop */
/* eslint-disable no-bitwise */
// ViscaCommand.ts

import { sendViscaCommandToDevice } from '../recorder/RecorderApi';
import { ViscaResponse } from '../recorder/ViscaTypes';
import { snooze } from '../util/Util';
import {
  ExposureMode,
  getViscaIP,
  getViscaPort,
  CameraState,
} from './ViscaState';

export const irisLabels = [
  'min',
  'f/14.0',
  'f/11.0',
  'f/9.6',
  'f/8.0',
  'f/6.8',
  'f/5.6',
  'f/4.8',
  'f/4.0',
  'f/3.4',
  'f/2.8',
  'f/2.4',
  'f/2.0',
  'f/1.8',
];

export const shutterLabels = [
  '1/1s",',
  '1/2s',
  '1/4s',
  '1/8s',
  '1/15s',
  '1/30s',
  '1/60s',
  '1/90s',
  '1/100s',
  '1/125s',
  '1/180s',
  '1/250s',
  '1/350s',
  '1/500s',
  '1/725s',
  '1/1000s',
  '1/1500s',
  '1/2000s',
  '1/3000s',
  '1/4000s',
  '1/6000s',
  '1/10000s',
];

export type ViscaCommand =
  | { type: 'AUTO_FOCUS'; value: boolean }
  | { type: 'FOCUS_IN' }
  | { type: 'FOCUS_OUT' }
  | { type: 'FOCUS_ONCE' }
  | { type: 'FOCUS_RESET' }
  | { type: 'ZOOM_IN' }
  | { type: 'ZOOM_OUT' }
  | { type: 'ZOOM_RESET' }
  | { type: 'EXPOSURE_MODE'; value: ExposureMode }
  | { type: 'IRIS_UP' }
  | { type: 'IRIS_DOWN' }
  | { type: 'IRIS_RESET' }
  | { type: 'SHUTTER_UP' }
  | { type: 'SHUTTER_DOWN' }
  | { type: 'SHUTTER_RESET' }
  | { type: 'GAIN_UP' }
  | { type: 'GAIN_DOWN' }
  | { type: 'GAIN_RESET' }
  | { type: 'SET_IRIS'; value: number }
  | { type: 'SET_SHUTTER'; value: number }
  | { type: 'SET_GAIN'; value: number }
  | { type: 'SET_ZOOM'; value: number }
  | { type: 'SET_FOCUS'; value: number }
  | { type: 'SET_BRIGHTNESS'; value: number }
  // Value queries
  | { type: 'AUTO_FOCUS_VALUE' }
  | { type: 'FOCUS_VALUE' }
  | { type: 'ZOOM_VALUE' }
  | { type: 'EXPOSURE_MODE_VALUE' }
  | { type: 'IRIS_VALUE' }
  | { type: 'SHUTTER_VALUE' }
  | { type: 'GAIN_VALUE' }
  | { type: 'BRIGHTNESS_VALUE' };

export interface ViscaMessageProps {
  data: ViscaCommand;
}

/**
 * Builds a single VISCA packet for set-style commands (focus, iris, etc.).
 * For multi-command operations like SET_ALL_STATES or QUERY_ALL_STATES,
 * we handle them separately below.
 */
function buildViscaPacket(cmd: ViscaCommand): Uint8Array {
  switch (cmd.type) {
    case 'AUTO_FOCUS':
      return cmd.value
        ? new Uint8Array([0x81, 0x01, 0x04, 0x38, 0x02, 0xff]) // AF on
        : new Uint8Array([0x81, 0x01, 0x04, 0x38, 0x03, 0xff]); // AF off

    case 'FOCUS_IN':
      return new Uint8Array([0x81, 0x01, 0x04, 0x08, 0x22, 0xff]);
    case 'FOCUS_OUT':
      return new Uint8Array([0x81, 0x01, 0x04, 0x08, 0x32, 0xff]);
    case 'FOCUS_RESET':
      return new Uint8Array([0x81, 0x01, 0x04, 0x08, 0x00, 0xff]);
    case 'FOCUS_ONCE':
      return new Uint8Array([0x81, 0x01, 0x04, 0x18, 0x01, 0xff]);
    case 'ZOOM_IN':
      return new Uint8Array([0x81, 0x01, 0x04, 0x07, 0x22, 0xff]);
    case 'ZOOM_OUT':
      return new Uint8Array([0x81, 0x01, 0x04, 0x07, 0x32, 0xff]);
    case 'ZOOM_RESET':
      return new Uint8Array([0x81, 0x01, 0x04, 0x07, 0x00, 0xff]);

    case 'EXPOSURE_MODE': {
      const mode = Number(cmd.value);
      return new Uint8Array([0x81, 0x01, 0x04, 0x39, mode, 0xff]);
    }

    case 'IRIS_UP':
      return new Uint8Array([0x81, 0x01, 0x04, 0x0b, 0x02, 0xff]);
    case 'IRIS_DOWN':
      return new Uint8Array([0x81, 0x01, 0x04, 0x0b, 0x03, 0xff]);
    case 'IRIS_RESET':
      return new Uint8Array([0x81, 0x01, 0x04, 0x0b, 0x00, 0xff]);
    case 'SHUTTER_UP':
      return new Uint8Array([0x81, 0x01, 0x04, 0x0a, 0x02, 0xff]);
    case 'SHUTTER_DOWN':
      return new Uint8Array([0x81, 0x01, 0x04, 0x0a, 0x03, 0xff]);
    case 'SHUTTER_RESET':
      return new Uint8Array([0x81, 0x01, 0x04, 0x0a, 0x00, 0xff]);
    case 'GAIN_UP':
      return new Uint8Array([0x81, 0x01, 0x04, 0x0c, 0x02, 0xff]);
    case 'GAIN_DOWN':
      return new Uint8Array([0x81, 0x01, 0x04, 0x0c, 0x03, 0xff]);
    case 'GAIN_RESET':
      return new Uint8Array([0x81, 0x01, 0x04, 0x0c, 0x00, 0xff]);

    case 'SET_IRIS':
      return new Uint8Array([
        0x81,
        0x01,
        0x04,
        0x4b,
        0x00,
        0x00,
        (cmd.value >> 4) & 0x0f,
        cmd.value & 0x0f,
        0xff,
      ]);
    case 'SET_SHUTTER':
      return new Uint8Array([
        0x81,
        0x01,
        0x04,
        0x4a,
        0x00,
        0x00,
        (cmd.value >> 4) & 0x0f,
        cmd.value & 0x0f,
        0xff,
      ]);
    case 'SET_GAIN':
      return new Uint8Array([
        0x81,
        0x01,
        0x04,
        0x4c,
        0x00,
        0x00,
        (cmd.value >> 4) & 0x0f,
        cmd.value & 0x0f,
        0xff,
      ]);
    case 'SET_BRIGHTNESS':
      return new Uint8Array([
        0x81,
        0x01,
        0x04,
        0x4d,
        0x00,
        0x00,
        (cmd.value >> 4) & 0x0f,
        cmd.value & 0x0f,
        0xff,
      ]);
    case 'SET_FOCUS':
      return new Uint8Array([
        0x81,
        0x01,
        0x04,
        0x48,
        (cmd.value >> 12) & 0x0f,
        (cmd.value >> 8) & 0x0f,
        (cmd.value >> 4) & 0x0f,
        cmd.value & 0x0f,
        0xff,
      ]);
    case 'SET_ZOOM':
      return new Uint8Array([
        0x81,
        0x01,
        0x04,
        0x47,
        (cmd.value >> 12) & 0x0f,
        (cmd.value >> 8) & 0x0f,
        (cmd.value >> 4) & 0x0f,
        cmd.value & 0x0f,
        0xff,
      ]);

    case 'AUTO_FOCUS_VALUE':
      return new Uint8Array([0x81, 0x09, 0x04, 0x38, 0xff]);
    case 'FOCUS_VALUE':
      return new Uint8Array([0x81, 0x09, 0x04, 0x48, 0xff]);
    case 'ZOOM_VALUE':
      return new Uint8Array([0x81, 0x09, 0x04, 0x47, 0xff]);
    case 'EXPOSURE_MODE_VALUE':
      return new Uint8Array([0x81, 0x09, 0x04, 0x39, 0xff]);
    case 'IRIS_VALUE':
      return new Uint8Array([0x81, 0x09, 0x04, 0x4b, 0xff]);
    case 'SHUTTER_VALUE':
      return new Uint8Array([0x81, 0x09, 0x04, 0x4a, 0xff]);
    case 'GAIN_VALUE':
      return new Uint8Array([0x81, 0x09, 0x04, 0x4c, 0xff]);
    case 'BRIGHTNESS_VALUE':
      return new Uint8Array([0x81, 0x09, 0x04, 0x4d, 0xff]);

    default:
      throw new Error(
        `buildViscaPacket() was called for ${JSON.stringify(cmd)}, which is unsupported.`,
      );
  }
}

/**
 * The main function that dispatches commands.
 * Returns `CameraState` when querying, or `void` for set operations.
 */
export const sendViscaCommand = async (
  cmd: ViscaCommand,
): Promise<ViscaResponse> => {
  // Normal single set command
  const port = getViscaPort();
  if (port === 0) {
    return { status: 'OK' };
  }
  const packet = buildViscaPacket(cmd);
  const ip = getViscaIP();
  if (ip) {
    return sendViscaCommandToDevice({ ip, port, data: packet });
  }
  return { status: 'Fail', msg: 'Visca IP Address not set' };
};

const extractViscaValue = (
  visca: ViscaResponse | undefined,
  numBytes: 1 | 2 | 4,
  defaultValue: number,
): number => {
  if (visca === undefined || !visca.data) {
    return defaultValue;
  }
  // console.log(JSON.stringify(visca));
  const start = visca.data.length - 1 - numBytes;
  if (start < 2) {
    return defaultValue;
  }
  switch (numBytes) {
    case 1:
      return visca.data[start];
    case 2:
      return (visca.data[start] << 4) | visca.data[start + 1];
    case 4:
      return (
        (visca.data[start] << 12) |
        (visca.data[start + 1] << 8) |
        (visca.data[start + 2] << 4) |
        visca.data[start + 3]
      );
    default:
      return defaultValue;
  }
};

export const getCameraState = async (): Promise<CameraState> => {
  const autoFocus =
    extractViscaValue(
      await sendViscaCommand({ type: 'AUTO_FOCUS_VALUE' }),
      1,
      2,
    ) === 2;

  const exposureMode = extractViscaValue(
    await sendViscaCommand({ type: 'EXPOSURE_MODE_VALUE' }),
    1,
    0,
  ) as ExposureMode;
  const iris = extractViscaValue(
    await sendViscaCommand({ type: 'IRIS_VALUE' }),
    2,
    8,
  );
  const shutter = extractViscaValue(
    await sendViscaCommand({ type: 'SHUTTER_VALUE' }),
    2,
    8,
  );
  const gain = extractViscaValue(
    await sendViscaCommand({ type: 'GAIN_VALUE' }),
    2,
    8,
  );
  const brightness = extractViscaValue(
    await sendViscaCommand({ type: 'BRIGHTNESS_VALUE' }),
    2,
    8,
  );
  const focus = extractViscaValue(
    await sendViscaCommand({ type: 'FOCUS_VALUE' }),
    4,
    8,
  );
  const zoom = extractViscaValue(
    await sendViscaCommand({ type: 'ZOOM_VALUE' }),
    4,
    8,
  );
  return {
    autoFocus,
    exposureMode,
    iris,
    shutter,
    gain,
    brightness,
    focus,
    zoom,
  };
};

export const updateCameraState = async (
  states: Partial<CameraState>,
): Promise<void> => {
  /**
   * We sequentially send the relevant “set” commands for each property that
   * is actually specified in `states`.
   */

  if (typeof states.exposureMode === 'number') {
    await snooze(50);
    await sendViscaCommand({
      type: 'EXPOSURE_MODE',
      value: states.exposureMode,
    });
  }

  if (typeof states.iris === 'number') {
    await snooze(50);
    await sendViscaCommand({
      type: 'SET_IRIS',
      value: states.iris,
    });
  }

  if (typeof states.shutter === 'number') {
    await snooze(50);
    await sendViscaCommand({
      type: 'SET_SHUTTER',
      value: states.shutter,
    });
  }

  if (typeof states.gain === 'number') {
    await snooze(50);
    await sendViscaCommand({
      type: 'SET_GAIN',
      value: states.gain,
    });
  }
  if (typeof states.brightness === 'number') {
    await snooze(50);
    await sendViscaCommand({
      type: 'SET_BRIGHTNESS',
      value: states.brightness,
    });
  }
  if (typeof states.autoFocus === 'boolean') {
    await snooze(50);
    await sendViscaCommand({
      type: 'AUTO_FOCUS',
      value: states.autoFocus,
    });
  }

  if (typeof states.zoom === 'number') {
    // Zoom needs to be finished before we set focus.
    // Poll zoom value until we reach specififed zoom then move on
    await sendViscaCommand({
      type: 'SET_ZOOM',
      value: states.zoom,
    });

    let zoom = 0;
    const startTime = Date.now();
    do {
      await snooze(1000);

      // Break if we've been looping for 10 seconds (10000 ms).
      if (Date.now() - startTime >= 10000) {
        console.warn(
          `Zoom operation timed out. current=${zoom}, target=${states.zoom}`,
        );
        break;
      }
      zoom = await extractViscaValue(
        await sendViscaCommand({ type: 'ZOOM_VALUE' }),
        4,
        8,
      );
    } while (Math.abs(zoom - states.zoom) > 100);
  }

  if (typeof states.focus === 'number') {
    // Note zoom must be stablized before applying focus
    await sendViscaCommand({
      type: 'SET_FOCUS',
      value: states.focus,
    });
  }
};
