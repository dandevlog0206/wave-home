import { useEffect, useMemo, useRef, useState } from 'react';
import './App.css';
import logoImage from './img/logo.png';
import logoWithStringImage from './img/logo_with_string.png';
import { api, formatRelativeTime } from './api';
import DevPage from './DevPage';

const navItems = [
  { id: 'main', label: 'Main', icon: '⌂' },
  { id: 'history', label: '제스처 히스토리', icon: '↺' },
  { id: 'gestures', label: '제스처 목록', icon: '✋' },
  { id: 'devices', label: 'IoT 상태', icon: '◈' },
  { id: 'developer', label: '개발자', icon: '⚙' },
];

function usePoll(fn, intervalMs, deps = []) {
  const [data, setData] = useState(null);
  const [error, setError] = useState(null);

  useEffect(() => {
    let cancelled = false;
    const run = async () => {
      try {
        const result = await fn();
        if (!cancelled) {
          setData(result);
          setError(null);
        }
      } catch (err) {
        if (!cancelled) setError(err);
      }
    };
    run();
    const id = setInterval(run, intervalMs);
    return () => {
      cancelled = true;
      clearInterval(id);
    };
  }, [intervalMs, ...deps]);

  return { data, error, refresh: fn };
}

function App() {
  const [activeView, setActiveView] = useState('main');
  const [logoClicks, setLogoClicks] = useState(0);
  const logoTimer = useRef(null);

  const [selectedDeviceId, setSelectedDeviceId] = useState(null);
  const [selectedGestureSetId, setSelectedGestureSetId] = useState('set0');
  const [gestureSetDetail, setGestureSetDetail] = useState(null);
  const [activeSetGesturesList, setActiveSetGesturesList] = useState([]);

  const { data: summary } = usePoll(api.summary, 1000);
  const { data: historyData } = usePoll(() => api.history({ limit: 50 }), 1000);
  const { data: setsData } = usePoll(api.gestureSets, 5000);
  const { data: devicesData } = usePoll(api.devices, 1000);
  const { data: bindingsData } = usePoll(api.bindings, 1000);

  const activeSetId = setsData?.activeSetId ?? 'set0';
  const gestureSets = setsData?.items ?? [];
  const activeGestureSet = gestureSets.find((s) => s.id === activeSetId) ?? gestureSets[0];
  const iotDevices = devicesData?.items ?? [];
  const selectedDevice = iotDevices.find((d) => d.id === selectedDeviceId) ?? iotDevices[0];

  useEffect(() => {
    if (iotDevices.length && !selectedDeviceId) {
      setSelectedDeviceId(iotDevices[0].id);
    }
  }, [iotDevices, selectedDeviceId]);

  useEffect(() => {
    if (activeSetId && !gestureSets.some((s) => s.id === selectedGestureSetId)) {
      setSelectedGestureSetId(activeSetId);
    }
  }, [activeSetId, gestureSets, selectedGestureSetId]);

  useEffect(() => {
    if (!selectedGestureSetId) return;
    let cancelled = false;
    api.gestureSet(selectedGestureSetId).then((detail) => {
      if (!cancelled) setGestureSetDetail(detail);
    });
    return () => {
      cancelled = true;
    };
  }, [selectedGestureSetId, bindingsData]);

  useEffect(() => {
    if (!activeSetId) return;
    let cancelled = false;
    api.gestureSet(activeSetId).then((detail) => {
      if (!cancelled) setActiveSetGesturesList(detail.gestures ?? []);
    });
    return () => {
      cancelled = true;
    };
  }, [activeSetId, bindingsData]);

  const bindingsByKey = useMemo(() => {
    const map = {};
    (bindingsData?.bindings ?? []).forEach((b) => {
      map[`${b.deviceId}-${b.controlId}`] = b;
    });
    return map;
  }, [bindingsData]);

  const gestureNameByClassId = useMemo(() => {
    const map = {};
    (gestureSetDetail?.gestures ?? []).forEach((g) => {
      map[g.gestureClassId] = g.name;
    });
    return map;
  }, [gestureSetDetail]);

  const historyItems = useMemo(
    () =>
      (historyData?.items ?? []).map((item) => ({
        id: item.id,
        gesture: item.gestureName,
        device: item.deviceName,
        action: item.actionLabel,
        time: formatRelativeTime(item.triggeredAt),
        confidence: item.confidence,
      })),
    [historyData]
  );

  const getBindingGestureName = (deviceId, controlId) => {
    const b = bindingsByKey[`${deviceId}-${controlId}`];
    return b?.gestureName ?? '';
  };

  const hasActiveControls = (device) =>
    device.controls?.some((ctrl) => {
      const id = ctrl.id ?? ctrl;
      return Boolean(getBindingGestureName(device.id, id));
    });

  const getDeviceControlState = (device) =>
    hasActiveControls(device) ? 'online' : 'inactive';

  const connectedDeviceCount = iotDevices.filter(
    (d) => d.connection === 'online' && hasActiveControls(d)
  ).length;

  const updateControlGesture = async (device, control, gestureClassId, gestureName) => {
    const controlId = control.id ?? control;
    const controlLabel = control.label ?? control;
    try {
      await api.putBinding({
        deviceId: device.id,
        controlId,
        controlLabel,
        gestureClassId: gestureClassId || null,
      });
    } catch {
      /* conflict */
    }
  };

  const deactivateSelectedDevice = async () => {
    if (selectedDevice) await api.clearDeviceBindings(selectedDevice.id);
  };

  const activateGestureSet = async (setId) => {
    await api.setActiveSet(setId);
    setSelectedGestureSetId(setId);
  };

  const handleLogoClick = () => {
    const next = logoClicks + 1;
    setLogoClicks(next);
    if (logoTimer.current) clearTimeout(logoTimer.current);
    logoTimer.current = setTimeout(() => setLogoClicks(0), 2000);
    if (next >= 10) {
      setLogoClicks(0);
      setActiveView('developer');
    }
  };

  const radarStatusLabel =
    summary?.radar?.status === 'ok' ? '정상' : summary?.radar?.connected ? '주의' : '오프라인';

  return (
    <div className="dashboard">
      <aside className="sidebar">
        <div className="brand">
          <button type="button" className="brand-mark brand-button" onClick={handleLogoClick}>
            <img src={logoImage} alt="WaveHome logo" />
          </button>
          <div>
            <strong>WaveHome</strong>
            <span>Radar Control</span>
          </div>
        </div>

        <nav className="nav-list" aria-label="Dashboard views">
          {navItems.flatMap((item) => {
            const nodes = [
              <button
                className={`nav-item ${item.id === 'developer' ? 'dev-nav' : ''} ${activeView === item.id ? 'active' : ''}`}
                key={item.id}
                onClick={() => setActiveView(item.id)}
                type="button"
              >
                <span aria-hidden="true">{item.icon}</span>
                <span className="nav-label">{item.label}</span>
              </button>,
            ];
            if (item.id === 'developer') {
              nodes.unshift(<hr key="dev-sep" className="nav-list-dev-sep" />);
            }
            return nodes;
          })}
        </nav>
      </aside>

      <main className="content">
        {activeView === 'main' && (
          <section className="view">
            <div className="hero">
              <div className="hero-copy">
                <span className="eyebrow">WAVEHOME DASHBOARD</span>
                <h1>파도에 몸을 맡기듯 당신의 집이 편안하도록, WaveHome</h1>
                <p>레이더 센서로 제스처를 인식하고 IoT 기기 상태를 한 화면에서 관리합니다.</p>
              </div>
              <div className="hero-logo-visual">
                <img src={logoWithStringImage} alt="WaveHome logo with text" />
              </div>
            </div>

            <div className="metric-grid">
              <Metric
                label="Radar 상태"
                value={radarStatusLabel}
                detail={summary?.radar?.detail ?? '—'}
                accent="radar"
              />
              <Metric
                label="오늘 인식"
                value={`${summary?.todayRecognitionCount ?? 0}회`}
                detail="서버 집계"
              />
              <Metric
                label="연결된 IoT"
                value={`${connectedDeviceCount}/${iotDevices.length || summary?.iot?.total || 0}`}
                detail="활성 제어가 있는 온라인 기기"
              />
              <Metric
                label="활성 제스처"
                value={activeGestureSet?.name ?? '—'}
                detail={`${activeGestureSet?.gestureCount ?? 0}개 제스처 사용 가능`}
              />
            </div>
          </section>
        )}

        {activeView === 'history' && (
          <section className="view">
            <PageHeader title="제스처 히스토리" description="그동안 인식된 제스처와 연결된 IoT 동작 기록입니다." />
            <Panel title="인식 로그">
              <HistoryList items={historyItems} />
            </Panel>
          </section>
        )}

        {activeView === 'gestures' && (
          <section className="view">
            <PageHeader title="제스처 목록" description="세트를 선택하면 아래에 제스처가 표시됩니다. 활성 세트는 IoT 제어에 사용됩니다." />

            <div className="gesture-set-grid">
              {gestureSets.map((set) => (
                <article
                  className={`gesture-set-card ${activeSetId === set.id ? 'active' : ''} ${selectedGestureSetId === set.id ? 'selected' : ''}`}
                  key={set.id}
                >
                  <button
                    type="button"
                    className="gesture-set-select"
                    onClick={() => setSelectedGestureSetId(set.id)}
                  >
                    <div>
                      <span className={`status-pill ${activeSetId === set.id ? 'success' : 'inactive'}`}>
                        {activeSetId === set.id ? '활성 세트' : '대기'}
                      </span>
                      <h2>{set.name}</h2>
                      <p>{set.description}</p>
                      <strong>{set.gestureCount}개 제스처</strong>
                    </div>
                  </button>
                  <div className="set-actions">
                    <button
                      type="button"
                      className={activeSetId === set.id ? 'active' : ''}
                      onClick={() => activateGestureSet(set.id)}
                    >
                      {activeSetId === set.id ? '활성화됨' : '활성화'}
                    </button>
                  </div>
                </article>
              ))}
            </div>

            {gestureSetDetail && selectedGestureSetId === gestureSetDetail.id && (
              <div className="gesture-set-detail">
                <div className="gesture-set-detail-header">
                  <div>
                    <span className="eyebrow">선택된 세트</span>
                    <h2>{gestureSetDetail.name}</h2>
                    <p>{gestureSetDetail.description}</p>
                  </div>
                </div>

                {gestureSetDetail.gestures?.length > 0 ? (
                  <div className="gesture-grid">
                    {gestureSetDetail.gestures.map((gesture) => (
                      <article className="gesture-card" key={gesture.gestureClassId}>
                        <div>
                          <span className={`status-pill ${gesture.status === 'active' ? 'success' : 'inactive'}`}>
                            {gesture.status === 'active' ? '활성' : '비활성'}
                          </span>
                          <div className="gesture-media" aria-label={`${gesture.name} media preview`}>
                            <div className="gesture-photo">
                              <img src={gesture.imageUrl} alt={gesture.name} loading="lazy" />
                            </div>
                          </div>
                          <h3>{gesture.name}</h3>
                        </div>
                      </article>
                    ))}
                  </div>
                ) : (
                  <div className="empty-state">
                    <strong>등록된 제스처가 없습니다.</strong>
                  </div>
                )}
              </div>
            )}
          </section>
        )}

        {activeView === 'developer' && <DevPage />}

        {activeView === 'devices' && selectedDevice && (
          <section className="view">
            <PageHeader title="IoT 목록과 상태" description="WaveHome과 연결된 기기의 연결 상태와 현재 전원 상태를 확인합니다." />
            <div className="active-set-banner">
              <span>활성 제스처 세트</span>
              <strong>{activeGestureSet?.name ?? '—'}</strong>
            </div>
            <div className="iot-control-layout">
              <Panel title="기기 목록">
                <DeviceList
                  items={iotDevices}
                  selectedId={selectedDevice.id}
                  onSelect={setSelectedDeviceId}
                  getControlState={getDeviceControlState}
                />
              </Panel>

              <Panel title={`${selectedDevice.name} 제어 설정`}>
                <div className="selected-device-summary">
                  <span className={`device-dot ${getDeviceControlState(selectedDevice)}`} />
                  <div>
                    <strong>{selectedDevice.state}</strong>
                    <span>{selectedDevice.room}</span>
                  </div>
                  <button className="deactivate-button" type="button" onClick={deactivateSelectedDevice}>
                    전체 비활성
                  </button>
                </div>

                <div className="control-list">
                  {selectedDevice.controls.map((control) => {
                    const controlId = control.id ?? control;
                    const controlLabel = control.label ?? control;
                    const binding = bindingsByKey[`${selectedDevice.id}-${controlId}`];
                    const currentClassId = binding?.gestureClassId ?? '';

                    return (
                      <label className="control-row" key={controlId}>
                        <span>{controlLabel}</span>
                        <select
                          value={currentClassId}
                          onChange={(e) => {
                            const val = e.target.value;
                            updateControlGesture(
                              selectedDevice,
                              control,
                              val ? Number(val) : 0,
                              ''
                            );
                          }}
                        >
                          <option value="">비활성</option>
                          {activeSetGesturesList.map((g) => {
                            const usedElsewhere = Object.entries(bindingsByKey).some(
                              ([key, b]) =>
                                key !== `${selectedDevice.id}-${controlId}` &&
                                b.gestureClassId === g.gestureClassId
                            );
                            return (
                              <option
                                key={g.gestureClassId}
                                value={g.gestureClassId}
                                disabled={usedElsewhere}
                              >
                                {g.name}
                                {usedElsewhere ? ' (사용 중)' : ''}
                              </option>
                            );
                          })}
                        </select>
                      </label>
                    );
                  })}
                </div>
              </Panel>
            </div>
          </section>
        )}
      </main>
    </div>
  );
}

function Metric({ label, value, detail, accent }) {
  return (
    <article className={`metric-card ${accent === 'radar' ? 'radar-metric' : ''}`}>
      <span>{label}</span>
      <strong>{value}</strong>
      <small>{detail}</small>
    </article>
  );
}

function Panel({ title, children }) {
  return (
    <section className="panel">
      <h2>{title}</h2>
      {children}
    </section>
  );
}

function PageHeader({ title, description }) {
  return (
    <header className="page-header">
      <span className="eyebrow">WaveHome Dashboard</span>
      <h1>{title}</h1>
      <p>{description}</p>
    </header>
  );
}

function HistoryList({ items }) {
  return (
    <div className="history-list">
      {items.map((item) => (
        <article className="history-item" key={item.id}>
          <div className="history-icon">✦</div>
          <div>
            <strong>{item.gesture}</strong>
            <span>
              {item.device} · {item.action}
            </span>
          </div>
          <time>{item.time}</time>
        </article>
      ))}
    </div>
  );
}

function DeviceList({ items, selectedId, onSelect, getControlState }) {
  return (
    <div className="device-list">
      {items.map((device) => (
        <button
          className={`device-row ${selectedId === device.id ? 'selected' : ''} selectable`}
          key={device.id}
          onClick={() => onSelect?.(device.id)}
          type="button"
        >
          <span className={`device-dot ${getControlState ? getControlState(device) : device.connection}`} />
          <div>
            <strong>{device.name}</strong>
            <span>{device.room}</span>
          </div>
          <span className="device-state">{device.state}</span>
        </button>
      ))}
    </div>
  );
}

export default App;
