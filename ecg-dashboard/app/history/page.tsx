'use client';
import { useEffect, useState } from 'react';
import { BarChart, Bar, XAxis, YAxis, Tooltip, ResponsiveContainer, Cell } from 'recharts';
import ClassBadge from '@/components/ClassBadge';
import TrendChart, { TrendPoint } from '@/components/TrendChart';
import { getLabelStyle } from '@/lib/labelColors';
import { TriangleAlert } from 'lucide-react';

interface Event {
  timestamp:        number;
  iso_timestamp:    string;
  cloud_label:      string;
  cloud_confidence: string;
  edge_label:       string;
  is_alert:         boolean;
  spo2?:            number;
  hr_ppg?:          number;
}

export default function HistoryPage() {
  const [events, setEvents]   = useState<Event[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    fetch('/api/events?limit=200')
      .then(r => r.json())
      .then(d => { setEvents(d.items ?? []); setLoading(false); })
      .catch(() => setLoading(false));
  }, []);

  const counts = events.reduce<Record<string, number>>((acc, e) => {
    const l = e.cloud_label || e.edge_label || 'Unknown';
    acc[l] = (acc[l] ?? 0) + 1;
    return acc;
  }, {});

  const chartData = Object.entries(counts).map(([label, count]) => ({
    label: getLabelStyle(label).vi,
    count,
    hex: getLabelStyle(label).hex,
  }));

  // Xu hướng SpO2 + nhịp tim theo thời gian (sort tăng dần, bỏ điểm 0/thiếu)
  const trend: TrendPoint[] = events
    .filter(e => (e.spo2 ?? 0) > 0 || (e.hr_ppg ?? 0) > 0)
    .map(e => {
      const d = e.iso_timestamp ? new Date(e.iso_timestamp) : new Date(e.timestamp * 1000);
      return {
        t:    d.getTime(),
        time: d.toLocaleTimeString('vi-VN', { hour: '2-digit', minute: '2-digit', timeZone: 'Asia/Ho_Chi_Minh' }),
        spo2: (e.spo2 ?? 0) > 0 ? e.spo2 : undefined,
        hr:   (e.hr_ppg ?? 0) > 0 ? e.hr_ppg : undefined,
      };
    })
    .sort((a, b) => a.t - b.t);

  return (
    <div className="max-w-4xl mx-auto space-y-5">
      <div>
        <h1 className="text-lg font-semibold tracking-tight">Lịch sử nhịp tim</h1>
        <p className="text-sm text-muted mt-0.5">{events.length} sự kiện gần nhất</p>
      </div>

      {/* Xu hướng SpO2 + nhịp tim */}
      <div className="bg-surface rounded-lg border border-border p-4">
        <p className="text-xs text-faint mb-2">Xu hướng SpO₂ &amp; nhịp tim</p>
        <TrendChart data={trend} />
      </div>

      {/* Bar chart phân bố loại nhịp */}
      {chartData.length > 0 && (
        <div className="bg-surface rounded-lg border border-border p-4">
          <p className="text-xs text-faint mb-4">Phân bố loại nhịp tim</p>
          <ResponsiveContainer width="100%" height={180}>
            <BarChart data={chartData} margin={{ left: -10 }}>
              <XAxis dataKey="label" tick={{ fill: '#71717a', fontSize: 11 }} axisLine={{ stroke: '#ececec' }} tickLine={false} />
              <YAxis tick={{ fill: '#71717a', fontSize: 11 }} axisLine={false} tickLine={false} />
              <Tooltip
                cursor={{ fill: '#f4f4f5' }}
                contentStyle={{ background: '#ffffff', border: '1px solid #ececec', borderRadius: 8, fontSize: 12 }}
                labelStyle={{ color: '#18181b' }}
              />
              <Bar dataKey="count" radius={[4, 4, 0, 0]}>
                {chartData.map((d, i) => <Cell key={i} fill={d.hex} />)}
              </Bar>
            </BarChart>
          </ResponsiveContainer>
        </div>
      )}

      {loading && <div className="text-center py-16 text-faint text-sm">Đang tải…</div>}

      {/* Event list */}
      <div className="space-y-2">
        {events.map((e, i) => (
          <div key={i}
            className="bg-surface rounded-lg border border-border px-4 py-3 flex items-center justify-between"
          >
            <div className="flex items-center gap-3">
              {e.is_alert && <TriangleAlert className="w-3.5 h-3.5 text-danger shrink-0" strokeWidth={2} />}
              <ClassBadge label={e.cloud_label || e.edge_label} />
              <span className="text-xs text-muted">
                {Math.round(parseFloat(e.cloud_confidence ?? '0') * 100)}% tin cậy
              </span>
            </div>
            <p className="text-xs text-faint tabular-nums">
              {e.iso_timestamp
                ? new Date(e.iso_timestamp).toLocaleString('vi-VN', { timeZone: 'Asia/Ho_Chi_Minh' })
                : '—'}
            </p>
          </div>
        ))}
      </div>
    </div>
  );
}
