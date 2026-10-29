/* eslint-disable no-await-in-loop */
import { driveFocus, getLensPosition, ViscaAddress } from './ViscaAPI';
import { setToast } from '../components/Toast';

interface Target {
  focus: number;
  /** The camera the target was computed for; its moves never go to another. */
  camera: ViscaAddress;
}

/**
 * Drives focus for Distance focus. One worker serves the whole app, so a remounted panel can't
 * start a second one on the same lens. Each target is a new object, so a repeat request for
 * the same position still checks the lens; a running drive gives way to it.
 */
let target: Target | null = null;
let running: Promise<void> | null = null;

/** Whether a newer target, or a cancel, has replaced this one. */
const superseded = (goal: Target) => () => target !== goal;

const follow = async () => {
  try {
    let driven: Target | null = null;
    while (target !== null && target !== driven) {
      driven = target;
      const goal = driven;
      try {
        const focus = await getLensPosition('focus', goal.camera);
        // The target may have been replaced or cancelled while the camera answered.
        if (focus !== goal.focus && target === goal) {
          await driveFocus(goal.focus, superseded(goal), goal.camera);
        }
      } catch (error) {
        // A newer target still runs. A replaced or cancelled one may fail as the camera
        // changes, so only a failure of the current target is reported; it ends the loop.
        if (target === goal) {
          setToast({
            severity: 'error',
            msg: `Distance focus failed: ${error}`,
          });
        }
      }
    }
  } finally {
    // In the same step as the last check of target, so a new target can't be missed.
    running = null;
  }
};

/** Sends a camera's lens to a focus position, or with null cancels the drive. */
export const setDistanceFocusTarget = (
  focus: number | null,
  camera: ViscaAddress,
) => {
  target = focus === null ? null : { focus, camera };
  if (target !== null && !running) running = follow();
};

/** Cancels the drive and resolves once its last command, which stops the lens, has been sent. */
export const stopDistanceFocus = async () => {
  target = null;
  await running;
};
