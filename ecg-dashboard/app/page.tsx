'use client';
import { useEcgStream } from '@/lib/useEcgStream';
import EcgWaveform from '@/components/EcgWaveform';
import ClassBadge from '@/components/ClassBadge';
import ProbBar from '@/components/ProbBar';
import { TriangleAlert } from 'lucide-react';

export default function DashboardPage() {
  const { lastResult, lastAlert, waveform, connected, deviceOnline, bpm } = useEcgStream();

  const label = lastResult?.label ?? 'Normal';
  const conf  = lastResult ? Math.round(lastResult.confidence * 100) : 0;
  const probs = lastResult?.probs ?? [1, 0, 0, 0, 0];

  return (
    <div className="max-w-4xl mx-auto space-y-5">
      {/* Header */}
      <div className="flex items-end justify-between">
        <div>
          <h1 className="text-lg font-semibold tracking-tight">Dashboard</h1>
          <p className="text-sm text-muted mt-0.5">ecg-device-001 · realtime</p>
        </div>
        <div className="flex items-center gap-4">
          <StatusDot ok={connected} label="AWS" />
          <StatusDot ok={deviceOnline} label={deviceOnline ? 'Thiết bị online' : 'Thiết bị offline'} pulse={deviceOnline} />
        </div>
      </div>

      {/* Alert banner — chỉ chỗ này dùng đỏ */}
      {lastAlert && (
        <div className="flex items-start gap-3 bg-danger/5 border border-danger/20 rounded-lg p-4">
          <TriangleAlert className="w-4 h-4 text-danger mt-0.5 shrink-0" strokeWidth={2} />
          <div>
            <p className="text-sm font-medium text-danger">
              Cảnh báo xác nhận kép — {lastAlert.cloud_label}
            </p>
            <p className="text-xs text-danger/70 mt-1 tabular-nums">
              Edge: {lastAlert.edge_label} ({Math.round(lastAlert.edge_confidence * 100)}%) ·
              Cloud: {lastAlert.cloud_label} ({Math.round(lastAlert.cloud_confidence * 100)}%) ·
              {lastAlert.iso_timestamp}
            </p>
          </div>
        </div>
      )}

      {/* Metric cards */}
      <div className="grid grid-cols-3 gap-4">
        <Card label="Nhịp tim">
          <p className="text-3xl font-semibold tabular-nums tracking-tight">{bpm ?? '—'}</p>
          <p className="text-xs text-faint mt-1">BPM</p>
        </Card>
        <Card label="Phân loại">
          <div className="mt-0.5"><ClassBadge label={label} /></div>
          <p className="text-xs text-faint mt-2">Độ tin cậy {conf}%</p>
        </Card>
        <Card label="Trạng thái">
          <p className="text-sm font-medium">
            {lastResult?.leads_on === false ? 'Mất điện cực' : 'Tín hiệu ổn định'}
          </p>
          <p className="text-xs text-faint mt-1">
            {lastResult?.needs_cloud_check ? 'Đang gửi cloud kiểm tra' : 'Edge xác nhận'}
          </p>
        </Card>
      </div>

      {/* ECG Waveform */}
      <Card label="Sóng ECG realtime">
        <div className="mt-2">
          {waveform.length > 10
            ? <EcgWaveform samples={waveform} label={label} />
            : <div className="h-40 flex items-center justify-center text-faint text-sm border border-border rounded-lg bg-zinc-50">
                Đang chờ tín hiệu từ thiết bị…
              </div>
          }
        </div>
      </Card>

      {/* Probability bars */}
      <Card label="Xác suất phân loại (Edge)">
        <div className="mt-3"><ProbBar probs={probs} /></div>
      </Card>
    </div>
  );
}

/* ── Sub-components ────────────────────────────────────── */
function Card({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="bg-surface rounded-lg p-4 border border-border">
      <p className="text-xs text-faint mb-1">{label}</p>
      {children}
    </div>
  );
}

function StatusDot({ ok, label, pulse }: { ok: boolean; label: string; pulse?: boolean }) {
  return (
    <div className="flex items-center gap-1.5">
      <span className={`w-1.5 h-1.5 rounded-full ${ok ? 'bg-foreground' : 'bg-faint'} ${pulse ? 'animate-pulse' : ''}`} />
      <span className="text-xs text-muted">{label}</span>
    </div>
  );
}
