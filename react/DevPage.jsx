import { useEffect, useRef, useState } from 'react';
import { devStreamUrl } from './api';
import { COLORMAP_NAMES, embeddingDisplayValue, sampleColormap } from './colormaps';

function formatUptime(sec) {
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = sec % 60;
  if (h > 0) return `${h}시간 ${m}분 ${s}초`;
  if (m > 0) return `${m}분 ${s}초`;
  return `${s}초`;
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

function EmbeddingHeatmap({ values, embedDim, sequenceLength, colormap }) {
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
    return <div className="empty-state compact">시퀀스 임베딩 대기 중…</div>;
  }

  return (
    <div className="embedding-panel">
      <div className="embedding-axis-labels">
        <span>임베딩 차원 ↓</span>
        <span>시간 →</span>
      </div>
      <canvas ref={canvasRef} className="embedding-canvas" />
    </div>
  );
}

export default function DevPage() {
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

  return (
    <section className="view dev-view">
      <PageHeader
        title="개발자 · 실시간 디버그"
        description={`WebSocket ${wsState === 'live' ? '연결됨' : wsState} — 확률·트리거·임베딩을 실시간으로 확인합니다.`}
      />

      <div className="dev-grid">
        <article className="panel dev-card">
          <h2>서버 · 레이더</h2>
          <dl className="dev-kv">
            <div><dt>가동 시간</dt><dd>{formatUptime(snap?.serverUptimeSec ?? 0)}</dd></div>
            <div><dt>활성 세트</dt><dd>{snap?.activeSetId ?? '—'}</dd></div>
            <div><dt>연결</dt><dd>{snap?.radar?.connected ? '연결됨' : '끊김'}</dd></div>
            <div><dt>IP</dt><dd>{snap?.radar?.ip || '—'}</dd></div>
            <div><dt>MAC</dt><dd>{snap?.radar?.mac || '—'}</dd></div>
            <div><dt>모델</dt><dd>{snap?.radar?.model || '—'}</dd></div>
            <div><dt>프레임율</dt><dd>{snap?.radar?.frameRateHz?.toFixed?.(1) ?? '0'} Hz</dd></div>
            <div><dt>타깃 수</dt><dd>{snap?.radar?.targetCount ?? 0}</dd></div>
          </dl>
        </article>

        <article className="panel dev-card dev-wide">
          <h2>클래스 확률 분포</h2>
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
            <h2>시퀀스 임베딩 궤적</h2>
            <label className="colormap-select">
              <span>컬러맵</span>
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
          />
        </article>

        <article className="panel dev-card dev-wide">
          <h2>트리거 채널</h2>
          <div className="trigger-grid">
            {(snap?.channels ?? []).map((ch) => (
              <div className="trigger-card" key={ch.gestureClassId}>
                <div className="trigger-card-head">
                  <strong>{labels[String(ch.gestureClassId)] ?? `class ${ch.gestureClassId}`}</strong>
                  <span className={`status-pill ${ch.toggleOutput ? 'success' : 'inactive'}`}>
                    {ch.toggleOutput ? 'ON' : 'OFF'}
                  </span>
                </div>
                <ThresholdBar
                  score={Math.max(0, ch.score)}
                  high={ch.highThreshold}
                  low={ch.lowThreshold}
                  state={ch.state}
                />
                <dl className="dev-kv compact">
                  <div><dt>상태</dt><dd>{ch.state}</dd></div>
                  <div><dt>점수</dt><dd>{ch.score?.toFixed(3)}</dd></div>
                  <div><dt>High</dt><dd>{ch.highThreshold}</dd></div>
                  <div><dt>Low</dt><dd>{ch.lowThreshold}</dd></div>
                  <div><dt>쿨다운</dt><dd>{ch.cooldownMs} ms</dd></div>
                  <div><dt>High 유지</dt><dd>{ch.minHighHoldMs} ms</dd></div>
                  <div><dt>Low 유지</dt><dd>{ch.minLowHoldMs} ms</dd></div>
                  <div><dt>홀드 진행</dt><dd>{ch.holdProgressMs ?? 0} / {ch.holdRequiredMs ?? 0} ms</dd></div>
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
