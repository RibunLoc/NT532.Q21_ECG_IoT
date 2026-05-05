'use client';
import { useEcgStream } from '@/lib/useEcgStream';
import EcgWaveform from '@/components/EcgWaveform';
import ClassBadge from '@/components/ClassBadge';
import ProbBar from '@/components/ProbBar';

export default function DashboardPage() {
  const { lastResult, lastAlert, waveform, connected, deviceOnline, bpm } = useEcgStream();

  const label = lastResult?.label ?? 'Normal';
  const conf  = lastResult ? Math.round(lastResult.confidence * 100) : 0;
  const probs = lastResult?.probs ?? [1, 0, 0, 0, 0];

  return (
    <div className="max-w-4xl mx-auto space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-xl font-bold">Dashboard realtime</h1>
          <p className="text-sm text-gray-500">ecg-device-001</p>
        </div>
        <div className="flex items-center gap-3">
          {/* AWS WebSocket */}
          <div className="flex items-center gap-1">
            <span className={`w-2 h-2 rounded-full ${connected ? 'bg-blue-400' : 'bg-gray-600'}`} />
            <span className="text-xs text-gray-500">AWS</span>
          </div>
          {/* ESP32 device */}
          <div className="flex items-center gap-1">
            <span className={`w-2 h-2 rounded-full ${deviceOnline ? 'bg-green-500 animate-pulse' : 'bg-red-500'}`} />
            <span className="text-xs text-gray-400">{deviceOnline ? 'Thiết bị online' : 'Thiết bị offline'}</span>
          </div>
        </div>
      </div>

      {/* Alert banner */}
      {lastAlert && (
        <div className="bg-red-900/30 border border-red-700 rounded-lg p-4">
          <p className="text-sm font-semibold text-red-300">
            ⚠️ Cảnh báo xác nhận kép — {lastAlert.cloud_label}
          </p>
          <p className="text-xs text-red-400 mt-1">
            Edge: {lastAlert.edge_label} ({Math.round(lastAlert.edge_confidence * 100)}%) ·
            Cloud: {lastAlert.cloud_label} ({Math.round(lastAlert.cloud_confidence * 100)}%) ·
            {lastAlert.iso_timestamp}
          </p>
        </div>
      )}

      {/* Metric cards */}
      <div className="grid grid-cols-3 gap-4">
        <div className="bg-gray-900 rounded-xl p-4 border border-gray-800">
          <p className="text-xs text-gray-500 uppercase tracking-wider mb-1">Nhịp tim</p>
          <p className="text-3xl font-bold text-white">{bpm ?? '—'}</p>
          <p className="text-xs text-gray-500 mt-1">BPM</p>
        </div>
        <div className="bg-gray-900 rounded-xl p-4 border border-gray-800">
          <p className="text-xs text-gray-500 uppercase tracking-wider mb-2">Phân loại</p>
          <ClassBadge label={label} />
          <p className="text-xs text-gray-500 mt-2">Độ tin cậy: {conf}%</p>
        </div>
        <div className="bg-gray-900 rounded-xl p-4 border border-gray-800">
          <p className="text-xs text-gray-500 uppercase tracking-wider mb-1">Trạng thái</p>
          <p className="text-sm font-semibold text-white">
            {lastResult?.leads_on === false ? '⚡ Mất điện cực' : '✅ Tín hiệu OK'}
          </p>
          <p className="text-xs text-gray-500 mt-1">
            {lastResult?.needs_cloud_check ? '☁️ Gửi cloud kiểm tra' : '✓ Edge xác nhận'}
          </p>
        </div>
      </div>

      {/* ECG Waveform */}
      <div className="bg-gray-900 rounded-xl p-4 border border-gray-800">
        <p className="text-xs text-gray-500 uppercase tracking-wider mb-3">Sóng ECG realtime</p>
        {waveform.length > 10
          ? <EcgWaveform samples={waveform} label={label} />
          : <div className="h-40 flex items-center justify-center text-gray-600 text-sm">
              Đang chờ tín hiệu từ thiết bị...
            </div>
        }
      </div>

      {/* Probability bars */}
      <div className="bg-gray-900 rounded-xl p-4 border border-gray-800">
        <p className="text-xs text-gray-500 uppercase tracking-wider mb-4">Xác suất phân loại (Edge)</p>
        <ProbBar probs={probs} />
      </div>

      {/* DEBUG PANEL — xóa sau khi fix xong */}
      <div className="bg-yellow-950/30 border border-yellow-800 rounded-xl p-4 text-xs font-mono space-y-1">
        <p className="text-yellow-500 font-bold mb-2">DEBUG (xóa sau khi fix)</p>
        <p className="text-gray-400">AWS connected: <span className="text-white">{String(connected)}</span></p>
        <p className="text-gray-400">Device online: <span className="text-white">{String(deviceOnline)}</span></p>
        <p className="text-gray-400">waveform.length: <span className="text-white">{waveform.length}</span></p>
        <p className="text-gray-400">lastResult: <span className="text-white">{lastResult ? JSON.stringify({ label: lastResult.label, conf: lastResult.confidence, hasSamples: !!(lastResult as {samples?:number[]}).samples?.length }) : 'null'}</span></p>
      </div>
    </div>
  );
}
