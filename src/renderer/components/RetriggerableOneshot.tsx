import { useCallback, useEffect, useRef } from 'react';

/**
 * Creates a retriggerable one-shot execution engine.
 * Provides a `trigger` function to schedule the callback execution,
 * and a `cancel` function to cancel the pending trigger with an option
 * to execute the callback immediately if a trigger was pending.
 *
 * @template T - The argument type(s) for the callback function.
 * @param fn - The callback function to execute.
 * @param delay - The delay in milliseconds before the callback is executed.
 * @returns An object with `trigger` and `cancel` functions.
 *
 * @example
 * const [ trigger, cancel ] = retriggerableOneShot(
 *   (message: string) => console.log(message),
 *   1000
 * );
 *
 * trigger("Hello"); // Starts a 1-second timer
 * cancel(true); // Cancels the timer and executes immediately
 */
function retriggerableOneShot<T>(
  fn: (...args: T[]) => void,
  delay: number,
): [(...args: T[]) => void, (executeIfPending: boolean) => void] {
  // References to track the timer and last arguments
  const timerId = useRef<NodeJS.Timeout | null>(null);
  const lastArgs = useRef<T[] | null>(null);

  /**
   * Triggers the one-shot execution with a delay.
   * If called again before the delay, the timer resets.
   *
   * @param args - Arguments to pass to the callback function.
   */
  const trigger = useCallback(
    (...args: T[]) => {
      lastArgs.current = args;
      if (timerId.current) {
        clearTimeout(timerId.current);
      }
      timerId.current = setTimeout(() => {
        if (lastArgs.current) {
          fn(...lastArgs.current);
        }
        timerId.current = null;
        lastArgs.current = null;
      }, delay);
    },
    [fn, delay],
  );

  /**
   * Cancels the pending trigger.
   * Optionally executes the callback immediately if a trigger was pending.
   *
   * @param executeIfPending - Whether to execute the callback immediately if a trigger is pending.
   */
  const cancel = useCallback(
    (executeIfPending: boolean) => {
      if (timerId.current) {
        clearTimeout(timerId.current);
        timerId.current = null;

        if (executeIfPending && lastArgs.current) {
          fn(...lastArgs.current);
          lastArgs.current = null;
        }
      }
    },
    [fn],
  );

  useEffect(() => {
    cancel(true);
  }, []);

  return [trigger, cancel];
}

export default retriggerableOneShot;
