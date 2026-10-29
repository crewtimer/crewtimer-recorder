/** Empirical focus model for one calibrated AIDA UHD-NDI3-X30.
 * Source: combined(2).csv; 104-unit rows, focus=6642 limit readings,
 * invalid fractional motor reading, and repeated CSV header excluded.
 * Coefficients are fitted offline; do not assume other camera units match.
 * Uses F(z,d) = A(z) + B(z)/d, where d is meters.
 * A and B are interpolated with monotonicity-preserving cubic Hermite (PCHIP).
 */

export type FocusPrediction = {
  focus: number | null;
  reason?: string;
};

// [actual VISCA zoom, fitted A, fitted B]. Fitting uses one averaged
// sample per distance at each zoom; negative slopes caused by noise at
// wide zoom were constrained to zero.
const CAL: ReadonlyArray<readonly [number, number, number]> = [
  [0, 10004.79373, 12.10902],
  [1820, 10364, 0],
  [3641, 10763.5, 0],
  [4096, 10868.49549, 99.54079],
  [5461, 11220.33333, 166.66667],
  [7282, 11768, 150],
  [8192, 12065.87924, 223.58029],
  [9102, 12389.66667, 233.33333],
  [10923, 13025, 800],
  [12288, 13430.70575, 1067.99464],
  [12743, 13493, 1050],
  [13000, 13478.70841, 1550.1831],
  [13500, 13377.28634, 2023.69113],
  [14000, 13119.67532, 2545.26862],
  [14500, 12641.95918, 4204.08163],
  [14564, 12637, 2550],
  [15000, 11893.44898, 5255.10204],
  [16384, 6401.24824, 14885.12081],
];

/** PCHIP node derivatives (Fritsch-Butland, matching scipy PchipInterpolator).
 * Slopes are calculated once when this module loads, not on each zoom update.
 */
function pchipSlopes(column: 1 | 2): number[] {
  const n = CAL.length;
  const h = Array.from({ length: n - 1 }, (_, i) => CAL[i + 1][0] - CAL[i][0]);
  const sec = h.map(
    (width, i) => (CAL[i + 1][column] - CAL[i][column]) / width,
  );
  const slopes = new Array<number>(n);

  for (let i = 1; i < n - 1; i += 1) {
    const left = sec[i - 1];
    const right = sec[i];
    if (left === 0 || right === 0 || Math.sign(left) !== Math.sign(right)) {
      slopes[i] = 0;
    } else {
      const w1 = 2 * h[i] + h[i - 1];
      const w2 = h[i] + 2 * h[i - 1];
      slopes[i] = (w1 + w2) / (w1 / left + w2 / right);
    }
  }

  // One-sided PCHIP endpoint derivatives, with monotonicity limiting.
  const endpoint = (h0: number, h1: number, d0: number, d1: number): number => {
    let result = ((2 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
    if (Math.sign(result) !== Math.sign(d0)) result = 0;
    else if (
      Math.sign(d0) !== Math.sign(d1) &&
      Math.abs(result) > 3 * Math.abs(d0)
    ) {
      result = 3 * d0;
    }
    return result;
  };
  slopes[0] = endpoint(h[0], h[1], sec[0], sec[1]);
  slopes[n - 1] = endpoint(h[n - 2], h[n - 3], sec[n - 2], sec[n - 3]);
  return slopes;
}

const A_SLOPES = pchipSlopes(1);
const B_SLOPES = pchipSlopes(2);

function hermite(
  y0: number,
  y1: number,
  m0: number,
  m1: number,
  h: number,
  t: number,
): number {
  const t2 = t * t;
  const t3 = t2 * t;
  return (
    (2 * t3 - 3 * t2 + 1) * y0 +
    (t3 - 2 * t2 + t) * h * m0 +
    (-2 * t3 + 3 * t2) * y1 +
    (t3 - t2) * h * m1
  );
}

function coefficients(zoom: number): { a: number; b: number } | null {
  if (
    !Number.isFinite(zoom) ||
    zoom < CAL[0][0] ||
    zoom > CAL[CAL.length - 1][0]
  )
    return null;

  let lo = 0;
  let hi = CAL.length - 1;
  while (hi - lo > 1) {
    // eslint-disable-next-line no-bitwise
    const mid = (lo + hi) >>> 1;
    if (CAL[mid][0] <= zoom) lo = mid;
    else hi = mid;
  }

  const [z0, a0, b0] = CAL[lo];
  if (zoom === z0) return { a: a0, b: b0 };
  const [z1, a1, b1] = CAL[lo + 1];
  const h = z1 - z0;
  const t = (zoom - z0) / h;
  return {
    a: hermite(a0, a1, A_SLOPES[lo], A_SLOPES[lo + 1], h, t),
    b: hermite(b0, b1, B_SLOPES[lo], B_SLOPES[lo + 1], h, t),
  };
}

/** Returns rounded VISCA focus position, or null outside supported model.
 * Restricts distance to 10..100 m (the calibration's measured range).
 * At full telephoto, 6642 was observed as an out-of-focus end stop; reject
 * predicted values at/beyond it. The boundary at other zooms is unknown.
 */
export function focusFromDistance(
  zoom: number,
  distanceMeters: number,
): number | null {
  if (
    !Number.isFinite(distanceMeters) ||
    distanceMeters < 10 ||
    distanceMeters > 100
  )
    return null;
  const c = coefficients(zoom);
  if (!c) return null;
  const f = Math.round(c.a + c.b / distanceMeters);
  if (zoom === 16384 && f <= 6642) return null;
  return f;
}

/** Returns a rough distance in meters or null if the inversion is unstable.
 * Requiring a 10-focus-unit separation from the infinity intercept is a
 * heuristic, NOT a measured uncertainty bound. Wide zoom generally has
 * poor range-estimation resolution. Do not use this as a rangefinder.
 */
export function distanceFromFocus(zoom: number, focus: number): number | null {
  if (!Number.isFinite(focus)) return null;
  if (zoom === 16384 && focus <= 6642) return null;
  const c = coefficients(zoom);
  if (!c || c.b <= 0) return null;
  const delta = focus - c.a;
  if (delta < 10) return null;
  const d = c.b / delta;
  return Number.isFinite(d) && d >= 10 && d <= 100 ? d : null;
}
