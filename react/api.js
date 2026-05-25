const API_BASE = '/api/v1';
let currentLocale = 'en-US';

async function fetchJson(path, options = {}) {
  const headers = {
    Accept: 'application/json',
    'X-Wave-Locale': currentLocale,
    ...(options.body ? { 'Content-Type': 'application/json' } : {}),
    ...options.headers,
  };
  const res = await fetch(`${API_BASE}${path}`, { ...options, headers });
  if (!res.ok) {
    const text = await res.text();
    throw new Error(text || `HTTP ${res.status}`);
  }
  if (res.status === 204) return null;
  return res.json();
}

export const api = {
  summary: () => fetchJson('/dashboard/summary'),
  history: (params = {}) => {
    const qs = new URLSearchParams();
    if (params.limit) qs.set('limit', String(params.limit));
    if (params.since) qs.set('since', params.since);
    const q = qs.toString();
    return fetchJson(`/gestures/history${q ? `?${q}` : ''}`);
  },
  gestureSets: () => fetchJson('/gesture-sets'),
  gestureSet: (setId) => fetchJson(`/gesture-sets/${setId}`),
  setActiveSet: (setId) =>
    fetchJson('/gesture-sets/active', {
      method: 'PUT',
      body: JSON.stringify({ setId }),
    }),
  devices: () => fetchJson('/devices'),
  bindings: () => fetchJson('/bindings'),
  putBinding: (payload) =>
    fetchJson('/bindings', { method: 'PUT', body: JSON.stringify(payload) }),
  clearDeviceBindings: (deviceId) =>
    fetchJson(`/bindings?deviceId=${encodeURIComponent(deviceId)}`, { method: 'DELETE' }),
  testDeviceControl: (deviceId, controlId) =>
    fetchJson(
      `/devices/${encodeURIComponent(deviceId)}/controls/${encodeURIComponent(controlId)}/test`,
      { method: 'POST' }
    ),
};

export function setApiLocale(localeTag) {
  currentLocale = localeTag || 'en-US';
}

export function devStreamUrl() {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${protocol}//${window.location.host}/api/v1/dev/stream`;
}

export function parseApiError(err) {
  if (!err?.message) return '요청에 실패했습니다.';
  try {
    const body = JSON.parse(err.message);
    return body?.error?.message ?? err.message;
  } catch {
    return err.message;
  }
}
