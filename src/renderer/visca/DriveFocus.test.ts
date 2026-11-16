/* eslint-disable no-bitwise */
import { driveFocus, resetFocusEdges, stepFocus } from './ViscaAPI';
import { sendViscaCommandToDevice } from '../recorder/RecorderApi';

jest.mock('../recorder/RecorderApi', () => ({
  sendViscaCommandToDevice: jest.fn(),
}));
jest.mock('../util/Util', () => ({ snooze: () => Promise.resolve() }));
jest.mock('./ViscaState', () => ({
  getViscaIP: () => '10.0.0.1',
  getViscaPort: () => 52381,
}));

const mockSend = sendViscaCommandToDevice as jest.MockedFunction<
  typeof sendViscaCommandToDevice
>;
const camera = { ip: '10.0.0.1', port: 52381 };

const reply = (value: number) => ({
  status: 'OK',
  data: new Uint8Array([
    0x90,
    0x50,
    (value >> 12) & 0xf,
    (value >> 8) & 0xf,
    (value >> 4) & 0xf,
    value & 0xf,
    0xff,
  ]),
});

/**
 * A lens that goes to any focus in its window [inf, near] it is sent, and to inf when sent
 * one outside it, as the X30 does. Records each focus position sent.
 */
const simulate = (focus: number, inf: number, near: number) => {
  const lens = { focus, inf, near, zoom: 11618, sent: [] as number[] };
  mockSend.mockImplementation(async ({ data }) => {
    if (data[1] === 0x01 && data[3] === 0x48) {
      const value = (data[4] << 12) | (data[5] << 8) | (data[6] << 4) | data[7];
      lens.sent.push(value);
      lens.focus = value >= lens.inf && value <= lens.near ? value : lens.inf;
    }
    if (data[1] === 0x09 && data[3] === 0x48) return reply(lens.focus);
    if (data[1] === 0x09 && data[3] === 0x47) return reply(lens.zoom);
    return { status: 'OK', data: new Uint8Array([0x90, 0x50, 0xff]) };
  });
  return lens;
};

describe('driveFocus', () => {
  beforeEach(() => {
    mockSend.mockReset();
    resetFocusEdges();
  });

  it('goes straight to an in-range position', async () => {
    const lens = simulate(13274, 13000, 13500);
    await expect(driveFocus(13226, undefined, camera)).resolves.toBe(13226);
    expect(lens.sent).toEqual([13226]);
  });

  it('does not move for a position it is already at', async () => {
    const lens = simulate(13274, 13000, 13500);
    await expect(driveFocus(13273, undefined, camera)).resolves.toBe(13274);
    expect(lens.sent).toEqual([]);
  });

  it('rests at inf when sent below it', async () => {
    const lens = simulate(13200, 13000, 13500);
    await expect(driveFocus(0, undefined, camera)).resolves.toBe(13000);
    expect(lens.sent).toEqual([0]);
  });

  it('finds the near edge once, then reuses it at the same zoom', async () => {
    const lens = simulate(13200, 13000, 13500);
    await expect(driveFocus(20000, undefined, camera)).resolves.toBe(13498);
    expect(lens.sent.length).toBe(15); // the try, 13 probes, the clamped position
    lens.sent = [];
    lens.focus = 13200;
    await expect(driveFocus(20000, undefined, camera)).resolves.toBe(13498);
    expect(lens.sent).toEqual([20000, 13498]);
  });

  it('searches again when inf has moved since the edge was found', async () => {
    const lens = simulate(13200, 13000, 13500);
    await driveFocus(20000, undefined, camera);
    lens.inf = 12900;
    lens.near = 13300;
    lens.sent = [];
    await expect(driveFocus(20000, undefined, camera)).resolves.toBe(13298);
    expect(lens.sent.length).toBe(15);
  });

  it('stops once cancelled', async () => {
    const lens = simulate(13200, 13000, 13500);
    await driveFocus(20000, () => lens.sent.length > 0, camera);
    expect(lens.sent).toEqual([20000]);
  });
});

describe('stepFocus', () => {
  beforeEach(() => {
    mockSend.mockReset();
    resetFocusEdges();
  });

  it('declines to step when the range at this zoom is unknown', async () => {
    const lens = simulate(13200, 13000, 13500);
    await expect(stepFocus(2)).resolves.toBe(false);
    await expect(stepFocus(-2)).resolves.toBe(false);
    expect(lens.sent).toEqual([]);
  });

  it('steps toward inf once inf is known', async () => {
    const lens = simulate(13200, 13000, 13500);
    await driveFocus(0, undefined, camera); // learns inf only
    lens.focus = 13200;
    lens.sent = [];
    await expect(stepFocus(-2)).resolves.toBe(true);
    await expect(stepFocus(2)).resolves.toBe(false); // near edge unknown
    expect(lens.sent).toEqual([13198]);
  });

  it('steps both ways inside the known window, not past its edges', async () => {
    const lens = simulate(13200, 13000, 13500);
    await driveFocus(20000, undefined, camera); // learns both edges; ends at 13498
    lens.sent = [];
    await expect(stepFocus(2)).resolves.toBe(false); // 13500 is past near - 2
    await expect(stepFocus(-2)).resolves.toBe(true);
    lens.focus = 13001;
    await expect(stepFocus(-2)).resolves.toBe(false); // below inf
    expect(lens.sent).toEqual([13496]);
  });

  it('declines at a zoom whose range is unknown', async () => {
    const lens = simulate(13200, 13000, 13500);
    await driveFocus(20000, undefined, camera);
    lens.zoom = 12000;
    lens.focus = 13200;
    lens.sent = [];
    await expect(stepFocus(2)).resolves.toBe(false);
    expect(lens.sent).toEqual([]);
  });
});
