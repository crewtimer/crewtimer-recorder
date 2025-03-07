/* eslint-disable no-bitwise */
// ViscaCommand.ts

import { UseDatum } from 'react-usedatum';
import { sendViscaCommandToDevice } from '../recorder/RecorderApi';

// Interface describing the camera's state
export interface CameraState {
  autoFocus: boolean;
  autoExposure: boolean;
  iris: number;
  shutter: number;
  gain: number;
}

export const [useViscaIP, , getViscaIp] = UseDatum('10.0.1.188');
export const [useViscaPort, , getViscaPort] = UseDatum(52381);

export type ViscaCommand =
  | { type: 'AUTO_FOCUS'; value: boolean }
  | { type: 'FOCUS_IN' }
  | { type: 'FOCUS_OUT' }
  | { type: 'ZOOM_IN' }
  | { type: 'ZOOM_OUT' }
  | { type: 'AUTO_EXPOSURE'; value: boolean }
  | { type: 'IRIS_UP' }
  | { type: 'IRIS_DOWN' }
  | { type: 'SHUTTER_UP' }
  | { type: 'SHUTTER_DOWN' }
  | { type: 'GAIN_UP' }
  | { type: 'GAIN_DOWN' }
  | { type: 'SET_IRIS'; value: number }
  | { type: 'SET_SHUTTER'; value: number }
  | { type: 'SET_GAIN'; value: number }

  // Commands for query
  | { type: 'QUERY_ALL_STATES' }

  // NEW: Set multiple states in one call
  | { type: 'SET_ALL_STATES'; value: Partial<CameraState> };

export interface ViscaMessageProps {
  data: ViscaCommand;
}

let queryAllStates: () => Promise<CameraState> = () => {
  return Promise.resolve({} as CameraState);
}; // forward

let setAllStates: (states: Partial<CameraState>) => Promise<void> = () => {
  return Promise.resolve();
}; // forward

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
      return new Uint8Array([0x81, 0x01, 0x04, 0x08, 0x02, 0xff]);
    case 'FOCUS_OUT':
      return new Uint8Array([0x81, 0x01, 0x04, 0x08, 0x03, 0xff]);
    case 'ZOOM_IN':
      return new Uint8Array([0x81, 0x01, 0x04, 0x07, 0x02, 0xff]);
    case 'ZOOM_OUT':
      return new Uint8Array([0x81, 0x01, 0x04, 0x07, 0x03, 0xff]);

    case 'AUTO_EXPOSURE':
      return cmd.value
        ? new Uint8Array([0x81, 0x01, 0x04, 0x39, 0x00, 0xff]) // AE on
        : new Uint8Array([0x81, 0x01, 0x04, 0x39, 0x01, 0xff]); // AE off

    case 'IRIS_UP':
      return new Uint8Array([0x81, 0x01, 0x04, 0x4b, 0x02, 0xff]);
    case 'IRIS_DOWN':
      return new Uint8Array([0x81, 0x01, 0x04, 0x4b, 0x03, 0xff]);
    case 'SHUTTER_UP':
      return new Uint8Array([0x81, 0x01, 0x04, 0x4a, 0x02, 0xff]);
    case 'SHUTTER_DOWN':
      return new Uint8Array([0x81, 0x01, 0x04, 0x4a, 0x03, 0xff]);
    case 'GAIN_UP':
      return new Uint8Array([0x81, 0x01, 0x04, 0x4c, 0x02, 0xff]);
    case 'GAIN_DOWN':
      return new Uint8Array([0x81, 0x01, 0x04, 0x4c, 0x03, 0xff]);

    case 'SET_IRIS':
      return new Uint8Array([
        0x81,
        0x01,
        0x04,
        0x4b,
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
        (cmd.value >> 4) & 0x0f,
        cmd.value & 0x0f,
        0xff,
      ]);

    // Multi-step commands we handle outside this function
    case 'QUERY_ALL_STATES':
    case 'SET_ALL_STATES':
      throw new Error(
        `buildViscaPacket() was called for ${cmd.type}, which requires special handling.`,
      );
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
export const sendViscaCommand = async ({
  data,
}: ViscaMessageProps): Promise<undefined | CameraState> => {
  switch (data.type) {
    case 'QUERY_ALL_STATES':
      // Query logic (multiple GET commands)
      return queryAllStates();

    case 'SET_ALL_STATES':
      // Set multiple states logic
      setAllStates(data.value);
      break;

    default: {
      // Normal single set command
      const packet = buildViscaPacket(data);
      console.log(`Sending '${data.type}'. Bytes:`, packet);
      const ip = getViscaIp();
      const port = getViscaPort();
      sendViscaCommandToDevice({ ip, port, data: packet });
    }
  }
  return undefined;
};

//
// Query logic (same as before, minimal placeholder for demonstration)
//
queryAllStates = async (): Promise<CameraState> => {
  // TODO: In real code, send inquiry commands, parse responses
  const autoFocus = true;
  const autoExposure = false;
  const iris = 11;
  const shutter = 12;
  const gain = 5;
  return { autoFocus, autoExposure, iris, shutter, gain };
};

//
// NEW: setAllStates logic
//
setAllStates = async (states: Partial<CameraState>): Promise<void> => {
  /**
   * We sequentially send the relevant “set” commands for each property that
   * is actually specified in `states`.
   */

  // For example:
  if (typeof states.autoFocus === 'boolean') {
    await sendViscaCommand({
      data: { type: 'AUTO_FOCUS', value: states.autoFocus },
    });
  }

  if (typeof states.autoExposure === 'boolean') {
    await sendViscaCommand({
      data: { type: 'AUTO_EXPOSURE', value: states.autoExposure },
    });
  }

  if (typeof states.iris === 'number') {
    await sendViscaCommand({
      data: { type: 'SET_IRIS', value: states.iris },
    });
  }

  if (typeof states.shutter === 'number') {
    await sendViscaCommand({
      data: { type: 'SET_SHUTTER', value: states.shutter },
    });
  }

  if (typeof states.gain === 'number') {
    await sendViscaCommand({
      data: { type: 'SET_GAIN', value: states.gain },
    });
  }
};
