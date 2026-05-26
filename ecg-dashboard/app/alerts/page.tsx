'use client';
import { useEffect, useState } from 'react';
import ClassBadge from '@/components/ClassBadge';
import { getLabelStyle } from '@/lib/labelColors';

interface Event {
  device_id:        string;
  timestamp:        number;
  iso_timestamp:    string;
  edge_label:       string;
  edge_confidence:  string;
  cloud_label:      string;
  cloud_confidence: string;
  is_alert:         boolean;
}

export default function AlertsPage() {
  const [events, setEvents]   = useState<Event[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    fetch('/api/events?alert=true&limit=100')
      .then(r => r.json())
      .then(d => { setEvents(d.items ?? []); setLoading(false); })
      .catch(() => setLoading(false));
  }, []);

  return (
    <div className="max-w-4xl mx-auto space-y-5">
      <div>
        <h1 className="text-lg font-semibold tracking-tight">Cảnh báo</h1>
        <p className="text-sm text-muted mt-0.5">Sự kiện loạn nhịp xác nhận bởi cả Edge và Cloud</p>
      </div>

      {loading && <div className="text-center py-16 text-faint text-sm">Đang tải…</div>}

      {!loading && events.length === 0 && (
        <div className="text-center py-16 text-faint text-sm border border-border rounded-lg bg-surface">
          Chưa có cảnh báo nào được ghi nhận.
        </div>
      )}

      <div className="space-y-2.5">
        {events.map((e, i) => {
          const s = getLabelStyle(e.cloud_label);
          return (
            <div key={i} className="bg-surface rounded-lg border border-border p-4">
              {/* dải màu mức độ bên trái */}
              <div className="flex items-start gap-3">
                <span
                  className="mt-1 w-1 self-stretch rounded-full shrink-0"
                  style={{ background: s.hex }}
                />
                <div className="flex-1 flex items-start justify-between gap-4">
                  <div className="space-y-1.5">
                    <div className="flex items-center gap-2">
                      <ClassBadge label={e.cloud_label} />
                      <span className="text-xs text-faint">Cloud xác nhận</span>
                    </div>
                    <p className="text-xs text-muted">
                      Edge <span className="text-foreground">{getLabelStyle(e.edge_label).vi}</span>
                      {' '}({Math.round(parseFloat(e.edge_confidence) * 100)}%)
                      {' · '}
                      Cloud <span className="text-foreground">{s.vi}</span>
                      {' '}({Math.round(parseFloat(e.cloud_confidence) * 100)}%)
                    </p>
                  </div>
                  <div className="text-right shrink-0">
                    <p className="text-xs text-muted">{e.device_id}</p>
                    <p className="text-xs text-faint mt-0.5 tabular-nums">
                      {e.iso_timestamp
                        ? new Date(e.iso_timestamp).toLocaleString('vi-VN', { timeZone: 'Asia/Ho_Chi_Minh' })
                        : '—'}
                    </p>
                  </div>
                </div>
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}
