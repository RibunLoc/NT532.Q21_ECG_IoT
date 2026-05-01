'use client';
import { useState, useRef } from 'react';
import ClassBadge from '@/components/ClassBadge';

const COLORS: Record<string, string> = {
  Normal:           'bg-green-500',
  Ventricular:      'bg-red-600',
  Supraventricular: 'bg-amber-600',
  Fusion:           'bg-blue-600',
  Unknown:          'bg-gray-500',
};

interface Stats {
  total:  number;
  alerts: number;
  counts: Record<string, number>;
}

export default function ReportPage() {
  const [loading,  setLoading]  = useState(false);
  const [report,   setReport]   = useState('');
  const [stats,    setStats]    = useState<Stats | null>(null);
  const [error,    setError]    = useState('');
  const abortRef = useRef<AbortController | null>(null);

  const handleGenerate = async () => {
    if (loading) {
      abortRef.current?.abort();
      setLoading(false);
      return;
    }

    setLoading(true);
    setReport('');
    setStats(null);
    setError('');

    const ctrl = new AbortController();
    abortRef.current = ctrl;

    try {
      const res = await fetch('/api/report?limit=200', { signal: ctrl.signal });

      if (!res.ok) {
        const body = await res.json();
        setError(body.error ?? 'Lỗi không xác định');
        setLoading(false);
        return;
      }

      setStats({
        total:  parseInt(res.headers.get('X-Stats-Total')  ?? '0'),
        alerts: parseInt(res.headers.get('X-Stats-Alerts') ?? '0'),
        counts: JSON.parse(res.headers.get('X-Stats-Counts') ?? '{}'),
      });

      const reader = res.body?.getReader();
      if (!reader) return;

      const decoder = new TextDecoder();
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        setReport(prev => prev + decoder.decode(value, { stream: true }));
      }
    } catch (err: unknown) {
      if ((err as Error).name !== 'AbortError') {
        setError(String(err));
      }
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="max-w-3xl mx-auto space-y-6">
      <div className="flex items-start justify-between gap-4">
        <div>
          <h1 className="text-xl font-bold">Báo cáo AI</h1>
          <p className="text-sm text-gray-500">Phân tích sức khỏe tim mạch tổng hợp bởi Claude AI</p>
        </div>
        <button
          onClick={handleGenerate}
          className={`shrink-0 px-4 py-2 rounded-lg text-sm font-medium transition-colors
            ${loading
              ? 'bg-red-900 hover:bg-red-800 text-red-300'
              : 'bg-blue-700 hover:bg-blue-600 text-white'}`}
        >
          {loading ? '⏹ Dừng' : '✦ Tạo báo cáo'}
        </button>
      </div>

      {stats && (
        <div className="bg-gray-900 rounded-xl border border-gray-800 p-4 space-y-4">
          <p className="text-xs text-gray-500 uppercase tracking-wider">Dữ liệu phân tích</p>
          <div className="grid grid-cols-3 gap-4 text-center">
            <div>
              <p className="text-2xl font-bold text-white">{stats.total}</p>
              <p className="text-xs text-gray-500 mt-0.5">Nhịp tim</p>
            </div>
            <div>
              <p className={`text-2xl font-bold ${stats.alerts > 0 ? 'text-red-400' : 'text-green-400'}`}>
                {stats.alerts}
              </p>
              <p className="text-xs text-gray-500 mt-0.5">Cảnh báo</p>
            </div>
            <div>
              <p className="text-2xl font-bold text-white">
                {stats.total > 0 ? ((stats.alerts / stats.total) * 100).toFixed(1) : '0'}%
              </p>
              <p className="text-xs text-gray-500 mt-0.5">Tỷ lệ bất thường</p>
            </div>
          </div>

          <div className="space-y-2 pt-1">
            {Object.entries(stats.counts).map(([label, count]) => {
              const pct = Math.round((count / stats.total) * 100);
              return (
                <div key={label} className="flex items-center gap-3">
                  <ClassBadge label={label} />
                  <div className="flex-1 bg-gray-800 rounded-full h-1.5 overflow-hidden">
                    <div
                      className={`h-full rounded-full ${COLORS[label] ?? 'bg-gray-500'}`}
                      style={{ width: `${pct}%` }}
                    />
                  </div>
                  <span className="text-xs text-gray-400 w-10 text-right">{pct}%</span>
                </div>
              );
            })}
          </div>
        </div>
      )}

      {error && (
        <div className="bg-red-950 border border-red-800 rounded-xl p-4 text-sm text-red-300">
          {error}
        </div>
      )}

      {(report || loading) && (
        <div className="bg-gray-900 rounded-xl border border-gray-800 p-5">
          <div className="flex items-center gap-2 mb-4">
            <span className="text-xs text-gray-500 uppercase tracking-wider">Phân tích từ Claude AI</span>
            {loading && (
              <span className="inline-flex gap-1">
                <span className="w-1 h-1 rounded-full bg-blue-400 animate-bounce [animation-delay:0ms]" />
                <span className="w-1 h-1 rounded-full bg-blue-400 animate-bounce [animation-delay:150ms]" />
                <span className="w-1 h-1 rounded-full bg-blue-400 animate-bounce [animation-delay:300ms]" />
              </span>
            )}
          </div>
          <div className="prose prose-invert prose-sm max-w-none text-gray-300 leading-relaxed whitespace-pre-wrap">
            {report}
            {loading && <span className="inline-block w-0.5 h-4 bg-blue-400 ml-0.5 animate-pulse align-text-bottom" />}
          </div>
        </div>
      )}

      {!report && !loading && !error && (
        <div className="text-center py-20 text-gray-600">
          <p className="text-4xl mb-3">🫀</p>
          <p className="text-sm">Nhấn "Tạo báo cáo" để AI phân tích dữ liệu ECG của bạn</p>
        </div>
      )}

      <p className="text-xs text-gray-700 text-center pb-4">
        Báo cáo này chỉ mang tính chất tham khảo và không thay thế chẩn đoán của bác sĩ chuyên khoa tim mạch.
      </p>
    </div>
  );
}
