import { useEffect, useRef, useState } from 'react';
import { devStreamUrl } from './api';
import { COLORMAP_NAMES, embeddingDisplayValue, sampleColormap } from './colormaps';
import InferenceProfilePlots from './InferenceProfilePlots';
import { createTranslator } from './i18n';

function formatLogTime(iso, localeTag, t) {
  if (!iso) return t('common.none');
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return iso;
  return d.toLocaleTimeString(localeTag, { hour12: false });
}

function formatRadarDetail(detail, reconnectSec, t) {
  if (!detail) return t('common.none');
  if (detail === 'radar.detail.live') return t('page.developer.radarDetail.live');
  if (detail === 'radar.detail.disconnected') return t('page.developer.radarDetail.disconnected');
  if (detail === 'radar.detail.connecting') {
    if (reconnectSec > 0) {
      return t('page.developer.radarDetail.connectingCountdown', { sec: reconnectSec });
    }
    return t('page.developer.radarDetail.connecting');
  }
  if (detail === 'radar.detail.scanning') return t('page.developer.radarDetail.scanning');
  return detail;
}

function formatLastPacket(iso, localeTag, t) {
  if (!iso) return t('common.none');
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return iso;
  return d.toLocaleTimeString(localeTag, { hour12: false });
}

function topGestureLabel(probs, labels) {
  if (!probs?.length) return null;
  let bestIdx = 0;
  let bestScore = probs[0] ?? 0;
  probs.forEach((score, idx) => {
    if (score > bestScore) {
      bestScore = score;
      bestIdx = idx;
    }
  });
  const name = labels[String(bestIdx)] ?? `class ${bestIdx}`;
  return `${name} (${(bestScore * 100).toFixed(1)}%)`;
}

function DevKvGrid({ groups }) {
  return (
    <div className="dev-status-grid">
      {groups.map((group) => (
        <section className="dev-status-group" key={group.title}>
          <h3>{group.title}</h3>
          <dl className="dev-kv dev-kv-columns">
            {group.items.map((item) => (
              <div key={item.label}>
                <dt>{item.label}</dt>
                <dd>{item.value}</dd>
              </div>
            ))}
          </dl>
        </section>
      ))}
    </div>
  );
}

function formatUptime(sec, localeTag) {
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = sec % 60;
  if (localeTag === 'ko-KR') {
    if (h > 0) return `${h}시간 ${m}분 ${s}초`;
    if (m > 0) return `${m}분 ${s}초`;
    return `${s}초`;
  }
  if (h > 0) return `${h}h ${m}m ${s}s`;
  if (m > 0) return `${m}m ${s}s`;
  return `${s}s`;
}

function ThresholdBar({ score, high, low, state }) {
  const pct = Math.max(0, Math.min(100, score * 100));
  const highPct = high * 100;
  const lowPct = low * 100;
  return (
    <div className="threshold-bar" aria-hidden="true">
      <div className="threshold-zone low" style={{ width: `${lowPct}%` }} />
      <div
        className="threshold-zone mid"
        style={{ left: `${lowPct}%`, width: `${Math.max(0, highPct - lowPct)}%` }}
      />
      <div className="threshold-marker high" style={{ left: `${highPct}%` }} title={`High ${high.toFixed(2)}`} />
      <div className="threshold-marker low" style={{ left: `${lowPct}%` }} title={`Low ${low.toFixed(2)}`} />
      <div className={`threshold-fill ${state}`} style={{ width: `${pct}%` }} />
    </div>
  );
}

function EmbeddingHeatmap({ values, embedDim, sequenceLength, colormap, t }) {
  const canvasRef = useRef(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || !values?.length || !embedDim || !sequenceLength) return;

    const displayRows = Math.min(embedDim, 64);
    const rowStride = Math.max(1, Math.floor(embedDim / displayRows));
    const cols = sequenceLength;

    const width = canvas.clientWidth || 600;
    const height = Math.max(180, displayRows * 4);
    canvas.width = width;
    canvas.height = height;

    const ctx = canvas.getContext('2d');
    const cellW = width / cols;
    const cellH = height / displayRows;

    for (let r = 0; r < displayRows; r += 1) {
      const srcR = Math.min(embedDim - 1, r * rowStride);
      for (let c = 0; c < cols; c += 1) {
        const raw = values[c * embedDim + srcR] ?? 0;
        const t = embeddingDisplayValue(raw);
        ctx.fillStyle = sampleColormap(colormap, t);
        ctx.fillRect(c * cellW, r * cellH, cellW + 0.5, cellH + 0.5);
      }
    }
  }, [values, embedDim, sequenceLength, colormap]);

  if (!values?.length || !sequenceLength) {
    return <div className="empty-state compact">{t('page.developer.embedding.wait')}</div>;
  }

  return (
    <div className="embedding-panel">
      <div className="embedding-axis-labels">
        <span>{t('page.developer.embeddingAxisY')}</span>
        <span>{t('page.developer.embeddingAxisX')}</span>
      </div>
      <canvas ref={canvasRef} className="embedding-canvas" />
    </div>
  );
}

export default function DevPage({ localeTag = 'en-US', t: providedT }) {
  const t = providedT ?? createTranslator(localeTag);
  const [snap, setSnap] = useState(null);
  const [wsState, setWsState] = useState('connecting');
  const [colormap, setColormap] = useState('Viridis');

  useEffect(() => {
    const ws = new WebSocket(devStreamUrl());
    ws.onopen = () => setWsState('live');
    ws.onclose = () => setWsState('closed');
    ws.onerror = () => setWsState('error');
    ws.onmessage = (ev) => {
      try {
        setSnap(JSON.parse(ev.data));
      } catch {
        /* ignore */
      }
    };
    return () => ws.close();
  }, []);

  const labels = snap?.classLabels ?? {};
  const probs = snap?.probabilities ?? [];
  const logs = snap?.logs ?? [];
  const radar = snap?.radar ?? {};
  const embedding = snap?.embedding ?? {};
  const devMeta = snap?.devMeta ?? {};
  const profileEnabled = Boolean(snap?.inferenceProfile?.enabled);

  const statusGroups = [
    {
      title: t('page.developer.statusGroup.server'),
      items: [
        { label: t('page.developer.uptime'), value: formatUptime(snap?.serverUptimeSec ?? 0, localeTag) },
        { label: t('page.developer.activeSet'), value: snap?.activeSetId ?? t('common.none') },
        {
          label: t('page.developer.websocket'),
          value: wsState === 'live' ? t('page.developer.websocket.live') : t(`page.developer.websocket.${wsState}`),
        },
        {
          label: t('page.developer.ncnnProfiling'),
          value: profileEnabled || devMeta.ncnnProfiling
            ? t('page.developer.enabled')
            : t('page.developer.disabled'),
        },
        { label: t('page.developer.bindingCount'), value: String(devMeta.bindingCount ?? 0) },
        { label: t('page.developer.todayGestures'), value: String(devMeta.todayGestureCount ?? 0) },
      ],
    },
    {
      title: t('page.developer.statusGroup.radar'),
      items: [
        {
          label: t('page.developer.connection'),
          value: radar.connected ? t('page.developer.connected') : t('page.developer.disconnected'),
        },
        { label: t('page.developer.radarStatus'), value: radar.status || t('common.none') },
        {
          label: t('page.developer.radarDetailLabel'),
          value: formatRadarDetail(radar.detail, radar.reconnectCountdownSec ?? 0, t),
        },
        { label: t('page.developer.ip'), value: radar.ip || t('common.none') },
        { label: t('page.developer.mac'), value: radar.mac || t('common.none') },
        { label: t('page.developer.model'), value: radar.model || t('common.none') },
        { label: t('page.developer.frameRate'), value: `${radar.frameRateHz?.toFixed?.(1) ?? '0'} Hz` },
        { label: t('page.developer.targetCount'), value: String(radar.targetCount ?? 0) },
        {
          label: t('page.developer.lastPacket'),
          value: formatLastPacket(radar.lastPacketAt, localeTag, t),
        },
      ],
    },
    {
      title: t('page.developer.statusGroup.inference'),
      items: [
        {
          label: t('page.developer.sequenceReady'),
          value: embedding.ready ? t('page.developer.ready') : t('page.developer.waiting'),
        },
        { label: t('page.developer.embedDim'), value: String(embedding.embedDim ?? 0) },
        { label: t('page.developer.sequenceLength'), value: String(embedding.sequenceLength ?? 0) },
        {
          label: t('page.developer.topGesture'),
          value: topGestureLabel(probs, labels) ?? t('common.none'),
        },
        {
          label: t('page.developer.triggerChannels'),
          value: String((snap?.channels ?? []).length),
        },
      ],
    },
  ];

  return (
    <section className="view dev-view">
      <PageHeader
        title={t('page.developer.title')}
        description={t('page.developer.description', {
          state: wsState === 'live' ? t('page.developer.websocket.live') : t(`page.developer.websocket.${wsState}`),
        })}
      />

      <div className="dev-grid">
        <article className="panel dev-card dev-wide dev-status-card">
          <h2>{t('page.developer.serverRadar')}</h2>
          <DevKvGrid groups={statusGroups} />
        </article>

        <article className="panel dev-card dev-wide">
          <h2>{t('page.developer.logs')}</h2>
          {logs.length > 0 ? (
            <div className="dev-log-panel" role="log" aria-live="polite">
              {[...logs].reverse().map((line, idx) => (
                <div className={`dev-log-line ${line.level ?? 'info'}`} key={`${line.at}-${idx}`}>
                  <time dateTime={line.at}>{formatLogTime(line.at, localeTag, t)}</time>
                  <span className="dev-log-level">{line.level}</span>
                  <span className="dev-log-message">{line.message}</span>
                </div>
              ))}
            </div>
          ) : (
            <div className="empty-state compact">{t('page.developer.logs.empty')}</div>
          )}
        </article>

        <InferenceProfilePlots profile={snap?.inferenceProfile} t={t} />

        <article className="panel dev-card dev-wide">
          <h2>{t('page.developer.probabilities')}</h2>
          <div className="prob-list">
            {probs.map((p, idx) => (
              <div className="prob-row" key={idx}>
                <span className="prob-label">{labels[String(idx)] ?? `class ${idx}`}</span>
                <div className="prob-track">
                  <div
                    className="prob-fill"
                    style={{ width: `${Math.min(100, Math.max(0, p) * 100)}%` }}
                  />
                </div>
                <span className="prob-value">{(Math.max(0, p) * 100).toFixed(1)}%</span>
              </div>
            ))}
          </div>
        </article>

        <article className="panel dev-card dev-wide">
          <div className="dev-section-head">
            <h2>{t('page.developer.embedding')}</h2>
            <label className="colormap-select">
              <span>{t('page.developer.colormap')}</span>
              <select value={colormap} onChange={(e) => setColormap(e.target.value)}>
                {COLORMAP_NAMES.map((name) => (
                  <option key={name} value={name}>
                    {name}
                  </option>
                ))}
              </select>
            </label>
          </div>
          <EmbeddingHeatmap
            values={snap?.embedding?.values}
            embedDim={snap?.embedding?.embedDim}
            sequenceLength={snap?.embedding?.sequenceLength}
            colormap={colormap}
            t={t}
          />
        </article>

        <article className="panel dev-card dev-wide">
          <h2>{t('page.developer.channels')}</h2>
          <div className="trigger-grid">
            {(snap?.channels ?? []).map((ch) => (
              <div className="trigger-card" key={ch.gestureClassId}>
                <div className="trigger-card-head">
                  <strong>{labels[String(ch.gestureClassId)] ?? `class ${ch.gestureClassId}`}</strong>
                  <span className={`status-pill ${ch.ready ? 'success' : 'inactive'}`}>
                    {ch.ready ? t('page.developer.channelReady') : t('page.developer.channelCooling')}
                  </span>
                </div>
                <ThresholdBar
                  score={Math.max(0, ch.score)}
                  high={ch.highThreshold}
                  low={ch.lowThreshold}
                  state={ch.state}
                />
                <dl className="dev-kv compact">
                  <div><dt>{t('page.developer.channelState')}</dt><dd>{ch.state}</dd></div>
                  <div><dt>{t('page.developer.channelScore')}</dt><dd>{ch.score?.toFixed(3)}</dd></div>
                  <div><dt>{t('page.developer.channelHigh')}</dt><dd>{ch.highThreshold}</dd></div>
                  <div><dt>{t('page.developer.channelLow')}</dt><dd>{ch.lowThreshold}</dd></div>
                  <div><dt>{t('page.developer.channelCooldown')}</dt><dd>{ch.cooldownMs} ms</dd></div>
                  <div><dt>{t('page.developer.channelMinHigh')}</dt><dd>{ch.minHighHoldMs} ms</dd></div>
                  <div><dt>{t('page.developer.channelMinLow')}</dt><dd>{ch.minLowHoldMs} ms</dd></div>
                  <div><dt>{t('page.developer.channelTriggerMode')}</dt><dd>{ch.triggerMode ?? 'pulse'}</dd></div>
                  <div><dt>{t('page.developer.channelRepeat')}</dt><dd>{ch.repeatIntervalMs ?? 0} ms</dd></div>
                  <div><dt>{t('page.developer.channelHold')}</dt><dd>{ch.holdProgressMs ?? 0} / {ch.holdRequiredMs ?? 0} ms</dd></div>
                </dl>
              </div>
            ))}
          </div>
        </article>
      </div>
    </section>
  );
}

function PageHeader({ title, description }) {
  return (
    <header className="page-header">
      <span className="eyebrow">Developer</span>
      <h1>{title}</h1>
      <p>{description}</p>
    </header>
  );
}
