import { useEffect, useRef } from 'react';

const PLOT_HEIGHT_PX = 200;
const HISTORY_LIMIT = 120;

function latestSample(samples) {
  if (!samples?.length) return null;
  return samples[samples.length - 1];
}

function formatLatestValue(value, unit) {
  if (value == null) return null;
  if (unit === '%') return value.toFixed(1);
  return value.toFixed(2);
}

function readPlotTheme(canvas) {
  const root = getComputedStyle(canvas);
  return {
    ink: root.getPropertyValue('--plot-ink').trim() || '#172022',
    muted: root.getPropertyValue('--plot-muted').trim() || '#6c777a',
    grid: root.getPropertyValue('--plot-grid').trim() || '#c5d0d3',
    surface: root.getPropertyValue('--plot-surface').trim() || '#f4f8f7',
    border: root.getPropertyValue('--plot-border').trim() || '#dde6e8',
  };
}

function drawLinePlot(canvas, plotWidth, samples, strokeColor, fillColor, unit) {
  if (!canvas) return;
  const data = (samples ?? []).slice(-HISTORY_LIMIT);
  const width = Math.max(1, plotWidth);
  const height = PLOT_HEIGHT_PX;
  const dpr = window.devicePixelRatio || 1;

  canvas.width = Math.floor(width * dpr);
  canvas.height = Math.floor(height * dpr);

  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, width, height);

  const theme = readPlotTheme(canvas);
  const padL = unit === '%' ? 48 : 52;
  const padR = 16;
  const padT = 16;
  const padB = 28;
  const plotW = width - padL - padR;
  const plotH = height - padT - padB;

  ctx.fillStyle = theme.surface;
  ctx.fillRect(0, 0, width, height);
  ctx.strokeStyle = theme.border;
  ctx.lineWidth = 1;
  ctx.strokeRect(0.5, 0.5, width - 1, height - 1);

  const yTicks = 4;
  ctx.strokeStyle = theme.grid;
  ctx.lineWidth = 1;
  ctx.font = '12px ui-monospace, SFMono-Regular, Menlo, monospace';
  ctx.textAlign = 'right';
  ctx.textBaseline = 'middle';

  if (data.length === 0) {
    ctx.fillStyle = theme.muted;
    ctx.font = '13px system-ui, sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('—', padL + plotW / 2, padT + plotH / 2);
    return;
  }

  const maxY = unit === '%' ? Math.max(10, ...data) : Math.max(0.5, ...data);
  const minY = 0;
  const rangeY = Math.max(maxY - minY, 0.001);

  for (let i = 0; i <= yTicks; i += 1) {
    const ratio = i / yTicks;
    const y = padT + plotH * ratio;
    const value = maxY - rangeY * ratio;

    ctx.beginPath();
    ctx.moveTo(padL, y);
    ctx.lineTo(width - padR, y);
    ctx.stroke();

    ctx.fillStyle = theme.ink;
    const label = unit === '%' ? `${value.toFixed(0)}` : `${value.toFixed(2)}`;
    ctx.fillText(label, padL - 8, y);
  }

  ctx.fillStyle = theme.muted;
  ctx.font = '11px system-ui, sans-serif';
  ctx.textAlign = 'center';
  ctx.textBaseline = 'top';
  ctx.fillText(unit, padL + plotW / 2, height - 10);

  const points = data.map((value, index) => {
    const x = padL + (plotW * index) / Math.max(1, data.length - 1);
    const y = padT + plotH - ((value - minY) / rangeY) * plotH;
    return { x, y, value };
  });

  const gradient = ctx.createLinearGradient(0, padT, 0, padT + plotH);
  gradient.addColorStop(0, fillColor);
  gradient.addColorStop(1, 'rgba(255, 255, 255, 0)');

  ctx.beginPath();
  points.forEach((point, index) => {
    if (index === 0) ctx.moveTo(point.x, point.y);
    else ctx.lineTo(point.x, point.y);
  });
  ctx.lineTo(points[points.length - 1].x, padT + plotH);
  ctx.lineTo(points[0].x, padT + plotH);
  ctx.closePath();
  ctx.fillStyle = gradient;
  ctx.fill();

  ctx.strokeStyle = strokeColor;
  ctx.lineWidth = 2.75;
  ctx.lineJoin = 'round';
  ctx.lineCap = 'round';
  ctx.beginPath();
  points.forEach((point, index) => {
    if (index === 0) ctx.moveTo(point.x, point.y);
    else ctx.lineTo(point.x, point.y);
  });
  ctx.stroke();

  const last = points[points.length - 1];
  ctx.fillStyle = theme.surface;
  ctx.strokeStyle = strokeColor;
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.arc(last.x, last.y, 5, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();
  ctx.beginPath();
  ctx.arc(last.x, last.y, 2.5, 0, Math.PI * 2);
  ctx.fillStyle = strokeColor;
  ctx.fill();
}

function InferencePlotCard({
  title,
  subtitle,
  latestMs,
  samples,
  strokeColor,
  fillColor,
  unit,
  latestLabel,
  t,
}) {
  const canvasRef = useRef(null);
  const wrapRef = useRef(null);
  const formattedLatest = formatLatestValue(latestMs, unit);

  useEffect(() => {
    const canvas = canvasRef.current;
    const wrap = wrapRef.current;
    if (!canvas || !wrap) return;

    const redraw = () => {
      const width = wrap.clientWidth;
      if (width <= 0) return;
      drawLinePlot(canvas, width, samples, strokeColor, fillColor, unit);
    };

    redraw();
    const observer = new ResizeObserver(redraw);
    observer.observe(wrap);
    return () => observer.disconnect();
  }, [samples, strokeColor, fillColor, unit]);

  return (
    <article className="panel dev-card inference-plot-card">
      <div className="dev-section-head">
        <div>
          <h2>{title}</h2>
          {subtitle ? <p className="inference-plot-subtitle">{subtitle}</p> : null}
        </div>
        <span className="inference-plot-latest">
          {formattedLatest == null
            ? t('page.developer.inferenceProfile.waiting')
            : latestLabel(formattedLatest)}
        </span>
      </div>
      <div className="inference-plot-wrap" ref={wrapRef}>
        <canvas ref={canvasRef} className="inference-plot-canvas" />
      </div>
    </article>
  );
}

export default function InferenceProfilePlots({ profile, t }) {
  if (!profile?.enabled) {
    return (
      <article className="panel dev-card dev-wide">
        <h2>{t('page.developer.inferenceProfile.title')}</h2>
        <div className="empty-state compact">{t('page.developer.inferenceProfile.disabled')}</div>
      </article>
    );
  }

  const frameEncoder = profile.frameEncoder ?? {};
  const temporalAggregator = profile.temporalAggregator ?? {};
  const combined = profile.combined ?? {};
  const cpu = profile.cpu ?? {};
  const aggregatorArch = (temporalAggregator.architecture ?? 'unknown').toUpperCase();

  const plots = [
    {
      key: 'pointnet',
      title: t('page.developer.inferenceProfile.pointnet'),
      subtitle: frameEncoder.name || 'PointNet',
      samples: frameEncoder.samplesMs,
      strokeColor: '#0a6b8a',
      fillColor: 'rgba(15, 139, 141, 0.22)',
      unit: 'ms',
      latestLabel: (v) => t('page.developer.inferenceProfile.latest', { ms: v }),
    },
    {
      key: 'aggregator',
      title: t('page.developer.inferenceProfile.aggregator', { arch: aggregatorArch }),
      subtitle: aggregatorArch,
      samples: temporalAggregator.samplesMs,
      strokeColor: '#0f8b8d',
      fillColor: 'rgba(117, 201, 183, 0.35)',
      unit: 'ms',
      latestLabel: (v) => t('page.developer.inferenceProfile.latest', { ms: v }),
    },
    {
      key: 'combined',
      title: t('page.developer.inferenceProfile.combined'),
      subtitle: t('page.developer.inferenceProfile.combinedHint'),
      samples: combined.samplesMs,
      strokeColor: '#c47a18',
      fillColor: 'rgba(243, 180, 78, 0.28)',
      unit: 'ms',
      latestLabel: (v) => t('page.developer.inferenceProfile.latest', { ms: v }),
    },
    {
      key: 'cpu',
      title: t('page.developer.inferenceProfile.cpu'),
      subtitle: t('page.developer.inferenceProfile.cpuHint'),
      samples: cpu.samplesPercent,
      strokeColor: '#b44a4a',
      fillColor: 'rgba(228, 93, 93, 0.22)',
      unit: '%',
      latestLabel: (v) => t('page.developer.inferenceProfile.latestPercent', { percent: v }),
    },
  ];

  return (
    <div className="inference-profile-grid">
      {plots.map((plot) => (
        <InferencePlotCard
          key={plot.key}
          title={plot.title}
          subtitle={plot.subtitle}
          latestMs={latestSample(plot.samples)}
          samples={plot.samples}
          strokeColor={plot.strokeColor}
          fillColor={plot.fillColor}
          unit={plot.unit}
          latestLabel={plot.latestLabel}
          t={t}
        />
      ))}
    </div>
  );
}
