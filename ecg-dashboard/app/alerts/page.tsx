'use client';
import { useEffect, useState } from 'react';
import ClassBadge from '@/components/ClassBadge';

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

const LABEL_VI: Record<string, string> = {
  Ventricular:      'Rung thất (VEB)',
  Supraventricular: 'Trên thất (SVE)',
  Fusion:           'Nhịp hỗn hợp',
  Normal:           'Bình thường',
  Unknown:          'Không xác định',
};

export default function AlertsPage() {
  const [events, setEvents]     = useState<Event[]>([]);
  const [loading, setLoading]   = useState(true);

  useEffect(() => {
    fetch('/api/events?alert=true&limit=100')
      .then(r => r.json())
      .then(d => { setEvents(d.items ?? []); setLoading(false); })
      .catch(() => setLoading(false));
  }, []);

  return (
    <div className="max-w-4xl mx-auto space-y-6">
      <div>
        <h1 className="text-xl font-bold">Cảnh báo</h1>
        <p className="text-sm text-gray-500">Các sự kiện loạn nhịp đã xác nhận bởi cả Edge và Cloud</p>
      </div>

      {loading && (
        <div className="text-center py-16 text-gray-500">Đang tải...</div>
      )}

      {!loading && events.length === 0 && (
        <div className="text-center py-16 text-gray-500">
          Chưa có cảnh báo nào được ghi nhận.
        </div>
      )}

      <div className="space-y-3">
        {events.map((e, i) => (
          <div key={i} className="bg-gray-900 rounded-xl border border-red-900/40 p-4">
            <div className="flex items-start justify-between gap-4">
              <div className="space-y-1">
                <div className="flex items-center gap-2">
                  <ClassBadge label={e.cloud_label} />
                  <span className="text-xs text-gray-500">Cloud xác nhận</span>
                </div>
                <p className="text-xs text-gray-400">
                  Edge: <span className="text-gray-300">{LABEL_VI[e.edge_label] ?? e.edge_label}</span>
                  {' '}({Math.round(parseFloat(e.edge_confidence) * 100)}%)
                  {' · '}
                  Cloud: <span className="text-gray-300">{LABEL_VI[e.cloud_label] ?? e.cloud_label}</span>
                  {' '}({Math.round(parseFloat(e.cloud_confidence) * 100)}%)
                </p>
              </div>
              <div className="text-right shrink-0">
                <p className="text-xs text-gray-500">{e.device_id}</p>
                <p className="text-xs text-gray-600 mt-0.5">
                  {e.iso_timestamp
                    ? new Date(e.iso_timestamp).toLocaleString('vi-VN', { timeZone: 'Asia/Ho_Chi_Minh' })
                    : '—'}
                </p>
              </div>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}
