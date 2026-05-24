function lerp(a, b, t) {
  return a + (b - a) * t;
}

function lerpRgb(c0, c1, t) {
  return [
    Math.round(lerp(c0[0], c1[0], t)),
    Math.round(lerp(c0[1], c1[1], t)),
    Math.round(lerp(c0[2], c1[2], t)),
  ];
}

function buildLut(stops) {
  const lut = new Array(256);
  for (let i = 0; i < 256; i += 1) {
    const t = i / 255;
    let j = 0;
    while (j < stops.length - 2 && t > stops[j + 1][0]) j += 1;
    const [t0, c0] = [stops[j][0], stops[j][1]];
    const [t1, c1] = [stops[j + 1][0], stops[j + 1][1]];
    const u = t1 > t0 ? (t - t0) / (t1 - t0) : 0;
    lut[i] = lerpRgb(c0, c1, u);
  }
  return lut;
}

const STOPS = {
  Magma: [
    [0, [0, 0, 4]],
    [0.25, [81, 18, 124]],
    [0.5, [183, 55, 121]],
    [0.75, [252, 164, 77]],
    [1, [252, 255, 164]],
  ],
  Inferno: [
    [0, [0, 0, 4]],
    [0.25, [87, 16, 110]],
    [0.5, [188, 55, 84]],
    [0.75, [249, 168, 37]],
    [1, [252, 255, 164]],
  ],
  Plasma: [
    [0, [13, 8, 135]],
    [0.25, [126, 3, 168]],
    [0.5, [204, 71, 120]],
    [0.75, [248, 149, 64]],
    [1, [240, 249, 33]],
  ],
  Viridis: [
    [0, [68, 1, 84]],
    [0.25, [59, 82, 139]],
    [0.5, [33, 145, 140]],
    [0.75, [94, 201, 98]],
    [1, [253, 231, 37]],
  ],
  Cividis: [
    [0, [0, 32, 76]],
    [0.25, [65, 78, 107]],
    [0.5, [124, 135, 99]],
    [0.75, [199, 189, 86]],
    [1, [255, 233, 69]],
  ],
  Twilight: [
    [0, [226, 220, 241]],
    [0.2, [158, 154, 200]],
    [0.4, [117, 107, 177]],
    [0.5, [79, 73, 155]],
    [0.6, [117, 107, 177]],
    [0.8, [158, 154, 200]],
    [1, [226, 220, 241]],
  ],
  Turbo: [
    [0, [48, 18, 59]],
    [0.25, [70, 107, 227]],
    [0.5, [35, 187, 138]],
    [0.75, [254, 224, 64]],
    [1, [122, 4, 3]],
  ],
  Berlin: [
    [0, [0, 80, 130]],
    [0.35, [220, 240, 255]],
    [0.5, [255, 255, 255]],
    [0.65, [255, 220, 200]],
    [1, [130, 0, 30]],
  ],
  Managua: [
    [0, [0, 40, 100]],
    [0.35, [0, 170, 160]],
    [0.65, [240, 230, 80]],
    [1, [180, 20, 60]],
  ],
  Vanimo: [
    [0, [20, 30, 120]],
    [0.35, [40, 160, 200]],
    [0.65, [240, 220, 120]],
    [1, [200, 60, 140]],
  ],
};

export const COLORMAP_NAMES = Object.keys(STOPS);

const LUTS = Object.fromEntries(
  COLORMAP_NAMES.map((name) => [name, buildLut(STOPS[name])])
);

export function sampleColormap(name, t) {
  const lut = LUTS[name] ?? LUTS.Viridis;
  const idx = Math.max(0, Math.min(255, Math.round(t * 255)));
  const [r, g, b] = lut[idx];
  return `rgb(${r},${g},${b})`;
}

export function embeddingDisplayValue(raw) {
  return Math.max(0, Math.min(1, 1.0 - Math.exp(-0.4 * raw)));
}
