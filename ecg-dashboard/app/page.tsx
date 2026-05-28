'use client';
import { useEffect, useRef, useState } from 'react';
import { useEcgStream } from '@/lib/useEcgStream';
import EcgWaveform from '@/components/EcgWaveform';
import ClassBadge from '@/components/ClassBadge';
import ProbBar from '@/components/ProbBar';
import { TriangleAlert } from 'lucide-react';
import { LineChart, Line, BarChart, Bar, XAxis, YAxis, ResponsiveContainer, Cell, Tooltip } from 'recharts';
import { LABEL_STYLE } from '@/lib/labelColors';

const BPM_HISTORY_MAX = 60;
const CLASS_ORDER = ['Normal', 'Supraventricular', 'Ventricular', 'Fusion', 'Unknown'];

export default function DashboardPage() {
  const { lastResult, lastAlert, lastVerified, verifiedAt, waveform, connected, deviceOnline, bpm } = useEcgStream();

  // Tinh thoi gian cloud verify gan nhat
  const [secAgo, setSecAgo] = useState(0);
  useEffect(() => {
    const id = setInterval(() => {
      setSecAgo(verifiedAt ? Math.round((Date.now() - verifiedAt) / 1000) : 0);
    }, 1000);
    return () => clearInterval(id);
  }, [verifiedAt]);

  const label = lastResult?.label ?? 'Normal';
  const conf  = lastResult ? Math.round(lastResult.confidence * 100) : 0;
  const probs = lastResult?.probs ?? [1, 0, 0, 0, 0];

  // ── Lịch sử BPM trong session (sparkline 60 điểm gần nhất) ──
  const [bpmHistory, setBpmHistory] = useState<{ i: number; bpm: number }[]>([]);
  const bpmIdxRef = useRef(0);
  useEffect(() => {
    if (bpm != null) {
      setBpmHistory(h => [...h, { i: bpmIdxRef.current++, bpm }].slice(-BPM_HISTORY_MAX));
    }
  }, [bpm]);

  // ── Đếm nhịp theo lớp trong session ──
  const [classCounts, setClassCounts] = useState<Record<string, number>>({});
  const lastResultRef = useRef<number>(0);
  useEffect(() => {
    if (lastResult && lastResult.timestamp !== lastResultRef.current) {
      lastResultRef.current = lastResult.timestamp;
      const l = lastResult.label;
      if (CLASS_ORDER.includes(l)) {
        setClassCounts(c => ({ ...c, [l]: (c[l] ?? 0) + 1 }));
      }
    }
  }, [lastResult]);

  const classChartData = CLASS_ORDER.map(k => ({
    label: LABEL_STYLE[k].vi,
    count: classCounts[k] ?? 0,
    hex:   LABEL_STYLE[k].hex,
  }));

  return (
    <div className="max-w-4xl mx-auto space-y-5">
      {/* Header */}
      <div className="flex items-end justify-between">
        <div>
          <h1 className="text-lg font-semibold tracking-tight">Dashboard</h1>
          <p className="text-sm text-muted mt-0.5">ecg-device-001 · realtime</p>
        </div>
        <div className="flex items-center gap-4">
          <StatusDot ok={connected}    label="AWS" />
          <StatusDot ok={deviceOnline} label={deviceOnline ? 'Thiết bị online' : 'Thiết bị offline'} pulse={deviceOnline} />
        </div>
      </div>

      {/* Alert banner */}
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
        <Card label="Cloud verify">
          {lastVerified ? (
            <>
              <p className="text-sm font-medium" style={{ color: lastVerified.agrees ? '#16a34a' : '#dc2626' }}>
                {lastVerified.cloud_label}
                {lastVerified.agrees ? ' ✓ khớp edge' : ' ✗ khác edge'}
              </p>
              <p className="text-xs text-faint mt-1 tabular-nums">
                conf {Math.round(lastVerified.cloud_confidence * 100)}% · {secAgo}s trước
                {lastVerified.is_alert && <span className="text-danger font-medium ml-1">· ALERT</span>}
              </p>
            </>
          ) : (
            <>
              <p className="text-sm font-medium text-faint">Chờ cloud verify</p>
              <p className="text-xs text-faint mt-1">
                {lastResult?.needs_cloud_check ? 'Đang gửi…' : 'Chưa có nhịp bất thường'}
              </p>
            </>
          )}
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

      {/* Xu hướng BPM (sparkline) + Phân bố nhịp trong session — 2 cột */}
      <div className="grid grid-cols-2 gap-4">
        <Card label={`Xu hướng nhịp tim (${bpmHistory.length}/60)`}>
          <div className="mt-2 h-32">
            {bpmHistory.length > 1
              ? <ResponsiveContainer width="100%" height="100%">
                  <LineChart data={bpmHistory} margin={{ top: 4, right: 4, bottom: 0, left: -28 }}>
                    <XAxis dataKey="i" hide />
                    <YAxis domain={[40, 160]} tick={{ fill: '#71717a', fontSize: 10 }} axisLine={false} tickLine={false} width={28} />
                    <Tooltip
                      contentStyle={{ background: '#fff', border: '1px solid #ececec', borderRadius: 6, fontSize: 11 }}
                      labelFormatter={() => ''}
                      formatter={(v) => [`${v} bpm`, '']}
                    />
                    <Line type="monotone" dataKey="bpm" stroke="#dc2626" strokeWidth={1.75} dot={false} isAnimationActive={false} />
                  </LineChart>
                </ResponsiveContainer>
              : <div className="h-full flex items-center justify-center text-faint text-xs">Chưa có dữ liệu nhịp</div>
            }
          </div>
        </Card>

        <Card label={`Phân bố nhịp trong phiên (${Object.values(classCounts).reduce((a,b) => a+b, 0)} nhịp)`}>
          <div className="mt-2 h-32">
            {Object.keys(classCounts).length > 0
              ? <ResponsiveContainer width="100%" height="100%">
                  <BarChart data={classChartData} margin={{ top: 4, right: 4, bottom: 0, left: -28 }}>
                    <XAxis dataKey="label" tick={{ fill: '#71717a', fontSize: 9 }} axisLine={{ stroke: '#ececec' }} tickLine={false} interval={0} />
                    <YAxis tick={{ fill: '#71717a', fontSize: 10 }} axisLine={false} tickLine={false} width={28} />
                    <Tooltip
                      cursor={{ fill: '#f4f4f5' }}
                      contentStyle={{ background: '#fff', border: '1px solid #ececec', borderRadius: 6, fontSize: 11 }}
                    />
                    <Bar dataKey="count" radius={[3, 3, 0, 0]}>
                      {classChartData.map((d, i) => <Cell key={i} fill={d.hex} />)}
                    </Bar>
                  </BarChart>
                </ResponsiveContainer>
              : <div className="h-full flex items-center justify-center text-faint text-xs">Chưa có nhịp được phân loại</div>
            }
          </div>
        </Card>
      </div>

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

// Chấm trạng thái: XANH lá khi OK, ĐỎ khi mất kết nối
function StatusDot({ ok, label, pulse }: { ok: boolean; label: string; pulse?: boolean }) {
  return (
    <div className="flex items-center gap-1.5">
      <span
        className={`w-2 h-2 rounded-full ${ok ? 'bg-green-500' : 'bg-red-500'} ${pulse && ok ? 'animate-pulse' : ''}`}
        title={ok ? 'Đang kết nối' : 'Mất kết nối'}
      />
      <span className={`text-xs ${ok ? 'text-foreground' : 'text-red-600 font-medium'}`}>{label}</span>
    </div>
  );
}
