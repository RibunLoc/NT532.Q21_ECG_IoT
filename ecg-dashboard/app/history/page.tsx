'use client';
import { useEffect, useState } from 'react';
import { BarChart, Bar, XAxis, YAxis, Tooltip, ResponsiveContainer, Cell } from 'recharts';
import ClassBadge from '@/components/ClassBadge';

interface Event {
  timestamp:        number;
  iso_timestamp:    string;
  cloud_label:      string;
  cloud_confidence: string;
  edge_label:       string;
  is_alert:         boolean;
}

const COLORS: Record<string, string> = {
  Normal:           '#16a34a',
  Ventricular:      '#A32D2D',
  Supraventricular: '#854F0B',
  Fusion:           '#185FA5',
  Unknown:          '#6b7280',
};

export default function HistoryPage() {
  const [events, setEvents]   = useState<Event[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    fetch('/api/events?limit=200')
      .then(r => r.json())
      .then(d => { setEvents(d.items ?? []); setLoading(false); })
      .catch(() => setLoading(false));
  }, []);

  // Thống kê theo loại
  const counts = events.reduce<Record<string, number>>((acc, e) => {
    const l = e.cloud_label || e.edge_label || 'Unknown';
    acc[l] = (acc[l] ?? 0) + 1;
    return acc;
  }, {});

  const chartData = Object.entries(counts).map(([label, count]) => ({ label, count }));

  return (
    <div className="max-w-4xl mx-auto space-y-6">
      <div>
        <h1 className="text-xl font-bold">Lịch sử nhịp tim</h1>
        <p className="text-sm text-gray-500">{events.length} sự kiện gần nhất</p>
      </div>

      {/* Bar chart */}
      {chartData.length > 0 && (
        <div className="bg-gray-900 rounded-xl border border-gray-800 p-4">
          <p className="text-xs text-gray-500 uppercase tracking-wider mb-4">Phân bố loại nhịp tim</p>
          <ResponsiveContainer width="100%" height={180}>
            <BarChart data={chartData} margin={{ left: -10 }}>
              <XAxis dataKey="label" tick={{ fill: '#9ca3af', fontSize: 11 }} />
              <YAxis tick={{ fill: '#9ca3af', fontSize: 11 }} />
              <Tooltip
                contentStyle={{ background: '#111827', border: '1px solid #374151', borderRadius: 8 }}
                labelStyle={{ color: '#f3f4f6' }}
              />
              <Bar dataKey="count" radius={[4, 4, 0, 0]}>
                {chartData.map((d, i) => (
                  <Cell key={i} fill={COLORS[d.label] ?? '#6b7280'} />
                ))}
              </Bar>
            </BarChart>
          </ResponsiveContainer>
        </div>
      )}

      {/* Event list */}
      {loading && <div className="text-center py-16 text-gray-500">Đang tải...</div>}

      <div className="space-y-2">
        {events.map((e, i) => (
          <div key={i}
            className={`bg-gray-900 rounded-lg border px-4 py-3 flex items-center justify-between
              ${e.is_alert ? 'border-red-900/50' : 'border-gray-800'}`}
          >
            <div className="flex items-center gap-3">
              {e.is_alert && <span className="text-red-400 text-xs">⚠️</span>}
              <ClassBadge label={e.cloud_label || e.edge_label} />
              <span className="text-xs text-gray-500">
                {Math.round(parseFloat(e.cloud_confidence ?? '0') * 100)}% tin cậy
              </span>
            </div>
            <p className="text-xs text-gray-600">
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
