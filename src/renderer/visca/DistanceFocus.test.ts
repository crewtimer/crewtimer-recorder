import { driveFocus, getLensPosition } from './ViscaAPI';
import { setDistanceFocusTarget, stopDistanceFocus } from './DistanceFocus';

jest.mock('./ViscaAPI', () => ({
  driveFocus: jest.fn(),
  getLensPosition: jest.fn(),
}));
jest.mock('../components/Toast', () => ({ setToast: jest.fn() }));

const mockDrive = driveFocus as jest.MockedFunction<typeof driveFocus>;
const mockPosition = getLensPosition as jest.MockedFunction<
  typeof getLensPosition
>;

const cameraA = { ip: '10.0.0.1', port: 52381 };
const cameraB = { ip: '10.0.0.2', port: 52381 };

/** A drive that runs until cancelled, then takes a while to send its final stop. */
const driveUntilCancelled = (log: string[]) =>
  mockDrive.mockImplementation(async (goal, cancelled) => {
    log.push(`drive ${goal}`);
    while (!cancelled?.()) {
      // eslint-disable-next-line no-await-in-loop
      await new Promise((resolve) => {
        setTimeout(resolve, 1);
      });
    }
    await new Promise((resolve) => {
      setTimeout(resolve, 5);
    });
    log.push(`reset ${goal}`);
    return goal;
  });

describe('Distance focus', () => {
  beforeEach(() => {
    mockDrive.mockReset();
    mockPosition.mockReset();
    mockPosition.mockResolvedValue(1000);
  });

  it('resolves a stop only after the drive has sent its final stop', async () => {
    const log: string[] = [];
    driveUntilCancelled(log);
    setDistanceFocusTarget(2000, cameraA);
    await new Promise((resolve) => {
      setTimeout(resolve, 5);
    });
    await stopDistanceFocus();
    log.push('manual move');
    expect(log).toEqual(['drive 2000', 'reset 2000', 'manual move']);
  });

  it('gives way to a new target and drives to it next', async () => {
    const log: string[] = [];
    driveUntilCancelled(log);
    setDistanceFocusTarget(2000, cameraA);
    await new Promise((resolve) => {
      setTimeout(resolve, 5);
    });
    setDistanceFocusTarget(3000, cameraA);
    await new Promise((resolve) => {
      setTimeout(resolve, 20);
    });
    await stopDistanceFocus();
    expect(log).toEqual([
      'drive 2000',
      'reset 2000',
      'drive 3000',
      'reset 3000',
    ]);
  });

  it('checks the lens again when the same position is requested', async () => {
    mockDrive.mockResolvedValue(2000);
    setDistanceFocusTarget(2000, cameraA);
    await stopDistanceFocus().then(() => undefined);
    mockPosition.mockClear();
    setDistanceFocusTarget(2000, cameraA);
    await new Promise((resolve) => {
      setTimeout(resolve, 5);
    });
    expect(mockPosition).toHaveBeenCalled();
  });

  it('does not start a move cancelled while the position was read', async () => {
    let answer: (focus: number) => void = () => {};
    mockPosition.mockImplementation(
      () =>
        new Promise((resolve) => {
          answer = resolve;
        }),
    );
    setDistanceFocusTarget(2000, cameraA);
    const stopped = stopDistanceFocus();
    answer(1000);
    await stopped;
    expect(mockDrive).not.toHaveBeenCalled();
  });

  it('keeps every command of a move on the camera it started on', async () => {
    const log: string[] = [];
    driveUntilCancelled(log);
    setDistanceFocusTarget(2000, cameraA);
    await new Promise((resolve) => {
      setTimeout(resolve, 5);
    });
    setDistanceFocusTarget(2500, cameraB);
    await new Promise((resolve) => {
      setTimeout(resolve, 20);
    });
    await stopDistanceFocus();
    expect(mockDrive.mock.calls.map((call) => [call[0], call[2]])).toEqual([
      [2000, cameraA],
      [2500, cameraB],
    ]);
    expect(mockPosition.mock.calls.map((call) => call[1])).toEqual([
      cameraA,
      cameraB,
    ]);
  });

  it('runs a newer target when the replaced one fails', async () => {
    let fail: (error: Error) => void = () => {};
    mockPosition
      .mockImplementationOnce(
        () =>
          new Promise((_resolve, reject) => {
            fail = reject;
          }),
      )
      .mockResolvedValue(1000);
    mockDrive.mockResolvedValue(2500);
    setDistanceFocusTarget(2000, cameraA);
    setDistanceFocusTarget(2500, cameraB);
    fail(new Error('no reply'));
    await new Promise((resolve) => {
      setTimeout(resolve, 5);
    });
    await stopDistanceFocus();
    expect(mockDrive.mock.calls.map((call) => [call[0], call[2]])).toEqual([
      [2500, cameraB],
    ]);
  });
});
