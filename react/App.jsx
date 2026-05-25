import { useEffect, useMemo, useRef, useState } from 'react';
import './App.css';
import logoImage from './img/logo.png';
import logoWithStringImage from './img/logo_with_string.png';
import { api, parseApiError, setApiLocale } from './api';
import DevPage from './DevPage';
import { createTranslator, detectSystemLocale, formatRelativeTime, SUPPORTED_LOCALES } from './i18n';

function makeNavItems(t) {
  return [
    { id: 'main', label: t('nav.main'), shortLabel: t('nav.main.short'), icon: '⌂' },
    { id: 'history', label: t('nav.history'), shortLabel: t('nav.history.short'), icon: '↺' },
    { id: 'gestures', label: t('nav.gestures'), shortLabel: t('nav.gestures.short'), icon: '✦' },
    { id: 'devices', label: t('nav.devices'), shortLabel: t('nav.devices.short'), icon: '◈' },
    { id: 'developer', label: t('nav.developer'), shortLabel: t('nav.developer.short'), icon: '⚙' },
  ];
}

function usePoll(fn, intervalMs, deps = []) {
  const [data, setData] = useState(null);
  const [error, setError] = useState(null);
  const [refreshTick, setRefreshTick] = useState(0);

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
  }, [intervalMs, refreshTick, ...deps]);

  const refresh = () => setRefreshTick((t) => t + 1);
  return { data, error, refresh };
}

function getRadarMetric(summary, t) {
  const radar = summary?.radar;
  if (!radar) return { value: t('common.none'), detail: t('common.none') };
  return {
    value: radar.status || t('common.none'),
    detail: radar.detail || t('common.none'),
  };
}

function normalizeBindingTriggerMode(value, fallback = 'pulse') {
  if (value === 'toggle' || value === 'repeat' || value === 'pulse') return value;
  return fallback;
}

function normalizeRepeatIntervalMs(value, fallback = 600) {
  const parsed = Number(value);
  if (!Number.isFinite(parsed)) return fallback;
  return Math.max(100, Math.round(parsed));
}

function App() {
  const [localeTag, setLocaleTag] = useState(detectSystemLocale());
  const [activeView, setActiveView] = useState('main');
  const [devUnlocked, setDevUnlocked] = useState(false);
  const [logoClicks, setLogoClicks] = useState(0);
  const logoTimer = useRef(null);

  const [selectedDeviceId, setSelectedDeviceId] = useState(null);
  const [selectedGestureSetId, setSelectedGestureSetId] = useState('set0');
  const [gestureSetDetail, setGestureSetDetail] = useState(null);
  const [activeSetGesturesList, setActiveSetGesturesList] = useState([]);
  const [activatingSetId, setActivatingSetId] = useState(null);
  const [activationError, setActivationError] = useState(null);
  const [controlTestState, setControlTestState] = useState({});
  const controlTestTimers = useRef({});
  const [bindingModal, setBindingModal] = useState(null);
  const t = useMemo(() => createTranslator(localeTag), [localeTag]);
  const navItems = useMemo(() => makeNavItems(t), [t]);

  useEffect(() => {
    setApiLocale(localeTag);
  }, [localeTag]);

  const { data: summary } = usePoll(api.summary, 1000, [localeTag]);
  const { data: historyData } = usePoll(() => api.history({ limit: 50 }), 1000, [localeTag]);
  const { data: setsData, refresh: refreshGestureSets } = usePoll(api.gestureSets, 5000, [localeTag]);
  const { data: devicesData } = usePoll(api.devices, 1000, [localeTag]);
  const { data: bindingsData } = usePoll(api.bindings, 1000, [localeTag]);

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

  const historyItems = useMemo(
    () =>
      (historyData?.items ?? []).map((item) => ({
        id: item.id,
        gesture: item.gestureName,
        device: item.deviceName,
        action: item.actionLabel,
        time: formatRelativeTime(item.triggeredAt, localeTag),
        confidence: item.confidence,
      })),
    [historyData, localeTag]
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

  const deactivateSelectedDevice = async () => {
    if (selectedDevice) await api.clearDeviceBindings(selectedDevice.id);
  };

  const runControlTest = async (deviceId, controlId) => {
    const key = `${deviceId}-${controlId}`;
    if (controlTestTimers.current[key]) {
      window.clearTimeout(controlTestTimers.current[key]);
      delete controlTestTimers.current[key];
    }
    try {
      await api.testDeviceControl(deviceId, controlId);
      setControlTestState((prev) => {
        if (!prev[key]) return prev;
        const next = { ...prev };
        delete next[key];
        return next;
      });
    } catch {
      setControlTestState((prev) => ({ ...prev, [key]: 'error' }));
      controlTestTimers.current[key] = window.setTimeout(() => {
        setControlTestState((prev) => {
          const next = { ...prev };
          delete next[key];
          return next;
        });
      }, 2000);
    }
  };

  const openBindingModal = (device, control) => {
    const controlId = control.id ?? control;
    const controlLabel = control.label ?? control;
    const currentBinding = bindingsByKey[`${device.id}-${controlId}`];
    const defaultMode = normalizeBindingTriggerMode(control.triggerMode, 'pulse');

    setBindingModal({
      deviceId: device.id,
      deviceName: device.name,
      controlId,
      controlLabel,
      gestureClassId: currentBinding?.gestureClassId ? String(currentBinding.gestureClassId) : '',
      triggerMode: normalizeBindingTriggerMode(currentBinding?.triggerMode, defaultMode),
      repeatIntervalMs: normalizeRepeatIntervalMs(currentBinding?.repeatIntervalMs, 600),
    });
  };

  const updateBindingModal = (patch) => {
    setBindingModal((prev) => (prev ? { ...prev, ...patch } : prev));
  };

  const closeBindingModal = () => {
    setBindingModal(null);
  };

  const saveBindingModal = async () => {
    if (!bindingModal) return;
    try {
      await api.putBinding({
        deviceId: bindingModal.deviceId,
        controlId: bindingModal.controlId,
        controlLabel: bindingModal.controlLabel,
        gestureClassId: bindingModal.gestureClassId ? Number(bindingModal.gestureClassId) : null,
        triggerMode: normalizeBindingTriggerMode(bindingModal.triggerMode, 'pulse'),
        repeatIntervalMs: normalizeRepeatIntervalMs(bindingModal.repeatIntervalMs, 600),
      });
      closeBindingModal();
    } catch {
      /* conflict */
    }
  };

  const activateGestureSet = async (setId) => {
    if (activatingSetId || setId === activeSetId) return;
    setActivatingSetId(setId);
    setActivationError(null);
    try {
      await api.setActiveSet(setId);
      setSelectedGestureSetId(setId);
      refreshGestureSets();
    } catch (err) {
      setActivationError(parseApiError(err));
    } finally {
      setActivatingSetId(null);
    }
  };

  const getSetActivationLabel = (setId) => {
    if (activatingSetId === setId) return t('page.gestures.activating');
    if (activeSetId === setId) return t('page.gestures.activated');
    return t('page.gestures.activate');
  };

  const getSetStatusPill = (setId) => {
    if (activatingSetId === setId) return { text: t('page.gestures.activating'), className: 'pending' };
    if (activeSetId === setId) return { text: t('page.gestures.activeSet'), className: 'success' };
    return { text: t('page.gestures.waiting'), className: 'inactive' };
  };

  useEffect(() => {
    if (activeView === 'developer' && !devUnlocked) {
      setActiveView('main');
    }
  }, [activeView, devUnlocked]);

  useEffect(() => () => {
    Object.values(controlTestTimers.current).forEach((timerId) => window.clearTimeout(timerId));
  }, []);

  const handleLogoClick = () => {
    const next = logoClicks + 1;
    setLogoClicks(next);
    if (logoTimer.current) clearTimeout(logoTimer.current);
    logoTimer.current = setTimeout(() => setLogoClicks(0), 2000);
    if (next >= 10) {
      setLogoClicks(0);
      setDevUnlocked(true);
      setActiveView('developer');
    }
  };

  const visibleNavItems = devUnlocked ? navItems : navItems.filter((item) => item.id !== 'developer');
  const radarMetric = getRadarMetric(summary, t);

  return (
    <div className={`dashboard locale-${localeTag.toLowerCase()}`}>
      <aside className="sidebar">
        <div className="brand">
          <div className="brand-mark brand-logo-hit" onClick={handleLogoClick} role="presentation">
            <img src={logoImage} alt="WaveHome logo" />
          </div>
          <div>
            <strong>{t('brand.name')}</strong>
            <span>{t('brand.subtitle')}</span>
          </div>
        </div>

        <DashboardNav
          items={visibleNavItems}
          activeView={activeView}
          onSelect={setActiveView}
          variant="sidebar"
        />

        <div className="sidebar-footer">
          <label className="language-select">
            <span>{t('language.label')}</span>
            <select value={localeTag} onChange={(e) => setLocaleTag(e.target.value)}>
              {SUPPORTED_LOCALES.map((locale) => (
                <option key={locale} value={locale}>
                  {t(`language.${locale}`)}
                </option>
              ))}
            </select>
          </label>
        </div>
      </aside>

      <main className="content">
        {activeView === 'main' && (
          <section className="view">
            <div className={`hero ${localeTag === 'en-US' ? 'hero-en' : ''}`}>
              <div className="hero-copy">
                <span className="eyebrow">{t('hero.eyebrow')}</span>
                <h1>{t('hero.title')}</h1>
                <p>{t('hero.description')}</p>
              </div>
              <div className="hero-logo-visual">
                <img src={logoWithStringImage} alt="WaveHome logo with text" />
              </div>
            </div>

            <div className="metric-grid">
              <Metric
                label={t('metric.radar')}
                value={radarMetric.value}
                detail={radarMetric.detail}
                accent="radar"
              />
              <Metric
                label={t('metric.today')}
                value={`${summary?.todayRecognitionCount ?? 0}`}
                detail={t('metric.today.detail')}
              />
              <Metric
                label={t('metric.iot')}
                value={`${connectedDeviceCount}/${iotDevices.length || summary?.iot?.total || 0}`}
                detail={t('metric.iot.detail')}
              />
              <Metric
                label={t('metric.activeGesture')}
                value={activeGestureSet?.name ?? t('common.none')}
                detail={t('metric.activeGesture.detail', { count: activeGestureSet?.gestureCount ?? 0 })}
              />
            </div>
          </section>
        )}

        {activeView === 'history' && (
          <section className="view">
            <PageHeader title={t('page.history.title')} description={t('page.history.description')} />
            <Panel title={t('page.history.panel')}>
              <HistoryList items={historyItems} icon={t('history.iconGesture')} />
            </Panel>
          </section>
        )}

        {activeView === 'gestures' && (
          <section className="view">
            <PageHeader title={t('page.gestures.title')} description={t('page.gestures.description')} />

            {activationError && (
              <div className="activation-error-banner" role="alert">
                {activationError}
              </div>
            )}

            <div className="gesture-set-grid">
              {gestureSets.map((set) => {
                const statusPill = getSetStatusPill(set.id);
                const isActiveOnServer = activeSetId === set.id;
                const isActivating = activatingSetId === set.id;
                return (
                  <article
                    className={`gesture-set-card ${isActiveOnServer ? 'active' : ''} ${selectedGestureSetId === set.id ? 'selected' : ''} ${isActivating ? 'activating' : ''}`}
                    key={set.id}
                  >
                    <button
                      type="button"
                      className="gesture-set-select"
                      onClick={() => setSelectedGestureSetId(set.id)}
                    >
                      <div>
                        <span className={`status-pill ${statusPill.className}`}>
                          {statusPill.text}
                        </span>
                        <h2>{set.name}</h2>
                        <p>{set.description}</p>
                        <strong>{t('metric.activeGesture.detail', { count: set.gestureCount })}</strong>
                      </div>
                    </button>
                    <div className="set-actions">
                      <button
                        type="button"
                        className={isActiveOnServer && !isActivating ? 'active' : ''}
                        disabled={Boolean(activatingSetId)}
                        onClick={() => activateGestureSet(set.id)}
                      >
                        {getSetActivationLabel(set.id)}
                      </button>
                    </div>
                  </article>
                );
              })}
            </div>

            {gestureSetDetail && selectedGestureSetId === gestureSetDetail.id && (
              <div className="gesture-set-detail">
                <div className="gesture-set-detail-header">
                  <div>
                    <span className="eyebrow">{t('page.gestures.selected')}</span>
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
                            {gesture.status === 'active' ? t('page.gestures.active') : t('page.gestures.inactive')}
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
                    <strong>{t('page.gestures.empty')}</strong>
                  </div>
                )}
              </div>
            )}
          </section>
        )}

        {activeView === 'developer' && <DevPage localeTag={localeTag} t={t} />}

        {activeView === 'devices' && selectedDevice && (
          <section className="view">
            <PageHeader title={t('page.devices.title')} description={t('page.devices.description')} />
            <div className="active-set-banner">
              <span>{t('page.devices.activeGestureSet')}</span>
              <strong>{activeGestureSet?.name ?? t('common.none')}</strong>
            </div>
            <div className="iot-control-layout">
              <Panel title={t('nav.devices')}>
                <DeviceList
                  items={iotDevices}
                  selectedId={selectedDevice.id}
                  onSelect={setSelectedDeviceId}
                  getControlState={getDeviceControlState}
                />
              </Panel>

              <Panel title={t('page.devices.controlSettings', { name: selectedDevice.name })}>
                <div className="selected-device-summary">
                  <span className={`device-dot ${getDeviceControlState(selectedDevice)}`} />
                  <div>
                    <strong>{selectedDevice.state}</strong>
                    <span>{selectedDevice.room}</span>
                  </div>
                  <button className="deactivate-button" type="button" onClick={deactivateSelectedDevice}>
                    {t('page.devices.deactivateAll')}
                  </button>
                </div>

                <div className="control-list">
                  {selectedDevice.controls.map((control) => {
                    const controlId = control.id ?? control;
                    const controlLabel = control.label ?? control;
                    const binding = bindingsByKey[`${selectedDevice.id}-${controlId}`];
                    const testKey = `${selectedDevice.id}-${controlId}`;
                    const testStatus = controlTestState[testKey];
                    const isBound = Boolean(binding?.gestureName);
                    const configLabel = binding?.gestureName ?? t('page.devices.configure');

                    return (
                      <div className="control-row" key={controlId}>
                        <div className="control-row-copy">
                          <span className="control-row-title">{controlLabel}</span>
                          <div className="control-row-status">
                            <span className={`binding-state-dot ${isBound ? 'active' : 'inactive'}`} />
                            <small>{isBound ? t('page.devices.binding.activeStatus') : t('page.devices.binding.inactiveStatus')}</small>
                          </div>
                        </div>
                        <div className="control-row-actions">
                          <button
                            type="button"
                            className="control-config-button"
                            onClick={() => openBindingModal(selectedDevice, control)}
                            aria-label={t('page.devices.openConfig')}
                            title={t('page.devices.openConfig')}
                          >
                            <span className="control-config-label">{configLabel}</span>
                          </button>
                          {devUnlocked && (
                            <button
                              type="button"
                              className={`control-test-button ${testStatus ?? ''}`}
                              onClick={() => runControlTest(selectedDevice.id, controlId)}
                            >
                              {testStatus === 'error' ? t('page.devices.test.error') : t('page.devices.test')}
                            </button>
                          )}
                        </div>
                      </div>
                    );
                  })}
                </div>
              </Panel>
            </div>
          </section>
        )}
      </main>

      <DashboardNav
        items={visibleNavItems}
        activeView={activeView}
        onSelect={setActiveView}
        variant="bottom"
      />

      {bindingModal && (
        <BindingConfigModal
          draft={bindingModal}
          bindingsByKey={bindingsByKey}
          gestures={activeSetGesturesList}
          onChange={updateBindingModal}
          onClose={closeBindingModal}
          onSave={saveBindingModal}
          t={t}
        />
      )}
    </div>
  );
}

function DashboardNav({ items, activeView, onSelect, variant }) {
  const isBottom = variant === 'bottom';
  return (
    <nav
      className={isBottom ? 'bottom-nav' : 'nav-list sidebar-nav'}
      aria-label={isBottom ? 'Mobile navigation' : 'Dashboard views'}
    >
      {items.flatMap((item) => {
        const nodes = [
          <button
            className={`nav-item ${item.id === 'developer' ? 'dev-nav' : ''} ${activeView === item.id ? 'active' : ''}`}
            key={item.id}
            onClick={() => onSelect(item.id)}
            type="button"
          >
            <span aria-hidden="true">{item.icon}</span>
            <span className="nav-label">{isBottom ? item.shortLabel : item.label}</span>
          </button>,
        ];
        if (!isBottom && item.id === 'developer') {
          nodes.unshift(<hr key="dev-sep" className="nav-list-dev-sep" />);
        }
        return nodes;
      })}
    </nav>
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
      <span className="eyebrow">WaveHome</span>
      <h1>{title}</h1>
      <p>{description}</p>
    </header>
  );
}

function HistoryList({ items, icon }) {
  return (
    <div className="history-list">
      {items.map((item) => (
        <article className="history-item" key={item.id}>
          <div className="history-icon">{icon}</div>
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

function BindingConfigModal({ draft, bindingsByKey, gestures, onChange, onClose, onSave, t }) {
  const currentKey = `${draft.deviceId}-${draft.controlId}`;

  return (
    <div className="binding-modal-backdrop" role="presentation" onClick={onClose}>
      <div
        className="binding-modal"
        role="dialog"
        aria-modal="true"
        aria-label={t('page.devices.binding.modalTitle', { name: draft.controlLabel })}
        onClick={(e) => e.stopPropagation()}
      >
        <div className="binding-modal-head">
          <div>
            <span className="eyebrow">{draft.deviceName}</span>
            <h2>{t('page.devices.binding.modalTitle', { name: draft.controlLabel })}</h2>
            <p>{t('page.devices.binding.modalDescription')}</p>
          </div>
          <button type="button" className="binding-modal-close" onClick={onClose} aria-label={t('page.devices.binding.cancel')}>
            ×
          </button>
        </div>

        <div className="binding-modal-body">
          <section className="binding-modal-section">
            <h3>{t('page.devices.binding.trigger')}</h3>
            <div className="binding-trigger-grid">
              {['pulse', 'toggle', 'repeat'].map((mode) => (
                <button
                  key={mode}
                  type="button"
                  className={`binding-trigger-card ${draft.triggerMode === mode ? 'active' : ''}`}
                  onClick={() => onChange({ triggerMode: mode })}
                >
                  <strong>{t(`page.devices.triggerMode.${mode}`)}</strong>
                  <span>{t(`page.devices.triggerMode.${mode}.description`)}</span>
                </button>
              ))}
            </div>

            {draft.triggerMode === 'repeat' && (
              <label className="binding-repeat-field">
                <span>{t('page.devices.binding.repeatInterval')}</span>
                <input
                  type="number"
                  min="100"
                  step="50"
                  value={draft.repeatIntervalMs}
                  onChange={(e) =>
                    onChange({ repeatIntervalMs: normalizeRepeatIntervalMs(e.target.value, 600) })
                  }
                />
                <small>{t('page.devices.binding.repeatHint')}</small>
              </label>
            )}
          </section>

          <section className="binding-modal-section">
            <h3>{t('page.devices.binding.gesture')}</h3>
            <div className="binding-gesture-grid">
              <button
                type="button"
                className={`binding-gesture-chip no-image ${draft.gestureClassId ? '' : 'active'}`}
                onClick={() => onChange({ gestureClassId: '' })}
              >
                <div className="binding-gesture-copy">
                  <span>{t('page.devices.binding.none')}</span>
                </div>
              </button>
              {gestures.map((gesture) => {
                const usedElsewhere = Object.entries(bindingsByKey).some(
                  ([key, binding]) =>
                    key !== currentKey && binding.gestureClassId === gesture.gestureClassId
                );
                const active = draft.gestureClassId === String(gesture.gestureClassId);
                return (
                  <button
                    type="button"
                    key={gesture.gestureClassId}
                    className={`binding-gesture-chip ${active ? 'active' : ''}`}
                    disabled={usedElsewhere}
                    onClick={() => onChange({ gestureClassId: String(gesture.gestureClassId) })}
                  >
                    {gesture.imageUrl ? (
                      <img
                        className="binding-gesture-thumb"
                        src={gesture.imageUrl}
                        alt={gesture.name}
                        loading="lazy"
                      />
                    ) : (
                      <div className="binding-gesture-thumb placeholder" aria-hidden="true" />
                    )}
                    <div className="binding-gesture-copy">
                      <span>{gesture.name}</span>
                      {usedElsewhere && <small>{t('page.devices.usedElsewhere')}</small>}
                    </div>
                  </button>
                );
              })}
            </div>
          </section>
        </div>

        <div className="binding-modal-actions">
          <button type="button" className="binding-modal-secondary" onClick={onClose}>
            {t('page.devices.binding.cancel')}
          </button>
          <button type="button" className="binding-modal-primary" onClick={onSave}>
            {t('page.devices.binding.save')}
          </button>
        </div>
      </div>
    </div>
  );
}

export default App;
