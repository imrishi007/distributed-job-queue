import { useCallback, useEffect, useState } from 'react';

const STALE_MS = 12000;

async function getJson(url) {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${url} -> HTTP ${res.status}`);
  return res.json();
}

const short = (id) => (id ? `${id.slice(0, 8)}...` : '');
const ago = (ts) => {
  if (!ts) return '-';
  const s = Math.round((Date.now() - new Date(ts).getTime()) / 1000);
  return `${s}s ago`;
};

const PAYLOAD_DEFAULTS = {
  echo: 'hello from the dashboard',
  sleep: '{"seconds":2}',
};

const PAYLOAD_HINTS = {
  echo: 'any text',
  sleep: 'JSON, e.g. {"seconds":2}',
};

function validatePayload(type, payload) {
  if (!payload.trim()) return 'payload is empty';
  if (type !== 'sleep') return null;
  let parsed;
  try {
    parsed = JSON.parse(payload);
  } catch {
    return `sleep needs ${PAYLOAD_HINTS.sleep}`;
  }
  if (typeof parsed.seconds !== 'number' || !Number.isFinite(parsed.seconds) || parsed.seconds <= 0) {
    return 'sleep needs a positive number of seconds';
  }
  return null;
}

export default function App() {
  const [health, setHealth] = useState({ ok: true, data: null });
  const [workers, setWorkers] = useState([]);
  const [jobs, setJobs] = useState([]);
  const [metrics, setMetrics] = useState({ queue_depth: 0, jobs_by_status: {} });
  const [form, setForm] = useState({ type: 'echo', payload: 'hello from the dashboard' });
  const [flash, setFlash] = useState('');

  const refresh = useCallback(async () => {
    try {
      const [h, w, j, m] = await Promise.all([
        getJson('/api/health'),
        getJson('/api/workers'),
        getJson('/api/jobs'),
        getJson('/api/metrics'),
      ]);
      setHealth({ ok: true, data: h });
      setWorkers(w);
      setJobs(j);
      setMetrics(m);
    } catch {
      setHealth({ ok: false, data: null });
    }
  }, []);

  useEffect(() => {
    refresh();
    const timer = setInterval(refresh, 3000);
    return () => clearInterval(timer);
  }, [refresh]);

  async function submit(e) {
    e.preventDefault();
    const problem = validatePayload(form.type, form.payload);
    if (problem) {
      setFlash(problem);
      setTimeout(() => setFlash(''), 5000);
      return;
    }
    try {
      const res = await fetch('/api/jobs', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(form),
      });
      const body = await res.json().catch(() => ({}));
      setFlash(res.ok ? `submitted job ${short(body.id)}` : `HTTP ${res.status}: ${body.error}`);
    } catch (err) {
      setFlash(`failed: ${err.message}`);
    }
    setTimeout(() => setFlash(''), 5000);
    refresh();
  }

  const workerState = (w) => {
    const age = (Date.now() - new Date(w.last_seen).getTime()) / 1000;
    if (age > STALE_MS / 1000) return { label: 'offline', color: '#c0392b' };
    return w.status === 'busy'
      ? { label: 'busy', color: '#b7950b' }
      : { label: 'online', color: '#1e8449' };
  };

  return (
    <div style={{ fontFamily: 'system-ui, sans-serif', maxWidth: 980, margin: '0 auto', padding: '0 20px 60px' }}>
      <h1>Distributed Job Queue</h1>
      <p style={{ color: health.ok ? '#1e8449' : '#c0392b', fontWeight: 600 }}>
        API: {health.ok ? 'online' : 'unreachable'}
      </p>
      <p>
        queue depth: <strong style={{ color: metrics.queue_depth ? '#b7950b' : '#1e8449' }}>{metrics.queue_depth}</strong>
        {' | '}
        {['queued', 'running', 'succeeded', 'failed']
          .filter((s) => metrics.jobs_by_status[s] !== undefined)
          .map((s) => `${s}: ${metrics.jobs_by_status[s]}`)
          .join(' | ')}
      </p>
      <hr />

      <h2>Submit a job</h2>
      <form onSubmit={submit} style={{ display: 'flex', gap: 8, flexWrap: 'wrap', marginBottom: 8 }}>
        <select
          value={form.type}
          onChange={(e) => {
            const prev = form.type;
            const next = e.target.value;
            setForm((f) => ({
              ...f,
              type: next,
              // Swap in the new type's default only if the user never
              // personalised the current payload.
              payload: f.payload === PAYLOAD_DEFAULTS[prev] ? PAYLOAD_DEFAULTS[next] : f.payload,
            }));
          }}
        >
          <option value="echo">echo</option>
          <option value="sleep">sleep</option>
        </select>
        <input
          style={{ flex: 1, minWidth: 260, padding: 4 }}
          value={form.payload}
          onChange={(e) => setForm({ ...form, payload: e.target.value })}
        />
        <button type="submit">Submit</button>
      </form>
      <p style={{ color: '#888', fontSize: 13, marginTop: 0 }}>
        payload ({PAYLOAD_HINTS[form.type]})
      </p>
      {flash && <p style={{ fontWeight: 600 }}>{flash}</p>}
      <hr />

      <h2>Workers</h2>
      <table style={{ width: '100%', borderCollapse: 'collapse' }}>
        <thead>
          <tr style={{ textAlign: 'left' }}>
            <th>name</th>
            <th>id</th>
            <th>state</th>
            <th>last seen</th>
          </tr>
        </thead>
        <tbody>
          {workers.map((w) => {
            const s = workerState(w);
            return (
              <tr key={w.id} style={{ borderTop: '1px solid #ddd' }}>
                <td>{w.name}</td>
                <td><code>{short(w.id)}</code></td>
                <td style={{ color: s.color, fontWeight: 600 }}>{s.label}</td>
                <td>{ago(w.last_seen)}</td>
              </tr>
            );
          })}
          {workers.length === 0 && (
            <tr>
              <td colSpan={4} style={{ color: '#888' }}>no workers registered</td>
            </tr>
          )}
        </tbody>
      </table>
      <hr />

      <h2>Recent jobs</h2>
      <table style={{ width: '100%', borderCollapse: 'collapse' }}>
        <thead>
          <tr style={{ textAlign: 'left' }}>
            <th>id</th>
            <th>type</th>
            <th>status</th>
            <th>output</th>
            <th>created</th>
          </tr>
        </thead>
        <tbody>
          {jobs.map((j) => (
            <tr key={j.id} style={{ borderTop: '1px solid #ddd' }}>
              <td><code>{short(j.id)}</code></td>
              <td>{j.type}</td>
              <td>{j.status}</td>
              <td style={{ wordBreak: 'break-all' }}>{j.output || ''}</td>
              <td>{ago(j.created_at)}</td>
            </tr>
          ))}
          {jobs.length === 0 && (
            <tr>
              <td colSpan={5} style={{ color: '#888' }}>no jobs yet</td>
            </tr>
          )}
        </tbody>
      </table>
    </div>
  );
}