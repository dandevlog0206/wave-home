const API_BASE = '/api/v1';

async function fetchJson(path, options = {}) {
  const headers = {
    Accept: 'application/json',
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
};

export function devStreamUrl() {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${protocol}//${window.location.host}/api/v1/dev/stream`;
}

export function formatRelativeTime(iso) {
  if (!iso) return '—';
  const then = new Date(iso).getTime();
  const diffSec = Math.max(0, Math.floor((Date.now() - then) / 1000));
  if (diffSec < 60) return '방금 전';
  if (diffSec < 3600) return `${Math.floor(diffSec / 60)}분 전`;
  if (diffSec < 86400) return `${Math.floor(diffSec / 3600)}시간 전`;
  return `${Math.floor(diffSec / 86400)}일 전`;
}
