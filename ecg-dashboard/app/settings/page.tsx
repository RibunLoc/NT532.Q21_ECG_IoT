'use client';
import { useState } from 'react';
import { Check } from 'lucide-react';

export default function SettingsPage() {
  const [vebThreshold,  setVebThreshold]  = useState('0.3');
  const [edgeThreshold, setEdgeThreshold] = useState('0.8');
  const [recipient,     setRecipient]     = useState('hothanhloc12345@gmail.com');
  const [saved,         setSaved]         = useState(false);

  const handleSave = () => {
    // TODO: gọi API để update Lambda env vars
    setSaved(true);
    setTimeout(() => setSaved(false), 2000);
  };

  return (
    <div className="max-w-xl mx-auto space-y-5">
      <div>
        <h1 className="text-lg font-semibold tracking-tight">Cài đặt</h1>
        <p className="text-sm text-muted mt-0.5">Ngưỡng cảnh báo và thông báo</p>
      </div>

      <div className="bg-surface rounded-lg border border-border p-5 space-y-5">
        <p className="text-sm font-medium">Ngưỡng phân loại</p>

        <div>
          <div className="flex items-center justify-between mb-1.5">
            <label className="text-xs text-muted">Cloud VEB Threshold</label>
            <span className="text-xs font-mono tabular-nums text-foreground">{vebThreshold}</span>
          </div>
          <input
            type="range" min="0.2" max="0.8" step="0.05"
            value={vebThreshold}
            onChange={e => setVebThreshold(e.target.value)}
            className="w-full accent-danger"
          />
          <p className="text-xs text-faint mt-1.5">
            Thấp hơn → nhạy hơn với VEB, nhiều cảnh báo giả hơn
          </p>
        </div>

        <div>
          <div className="flex items-center justify-between mb-1.5">
            <label className="text-xs text-muted">Edge Confidence Threshold</label>
            <span className="text-xs font-mono tabular-nums text-foreground">{edgeThreshold}</span>
          </div>
          <input
            type="range" min="0.5" max="0.95" step="0.05"
            value={edgeThreshold}
            onChange={e => setEdgeThreshold(e.target.value)}
            className="w-full accent-zinc-800"
          />
          <p className="text-xs text-faint mt-1.5">
            Cao hơn → edge chắc chắn hơn mới không gửi cloud
          </p>
        </div>
      </div>

      <div className="bg-surface rounded-lg border border-border p-5 space-y-3">
        <p className="text-sm font-medium">Thông báo email</p>
        <div>
          <label className="text-xs text-muted block mb-1.5">Email nhận cảnh báo</label>
          <input
            type="email"
            value={recipient}
            onChange={e => setRecipient(e.target.value)}
            className="w-full bg-background border border-border rounded-md px-3 py-2
                       text-sm text-foreground focus:outline-none focus:border-zinc-400 focus:ring-1 focus:ring-zinc-300"
          />
        </div>
      </div>

      <button
        onClick={handleSave}
        className={`w-full py-2.5 rounded-md text-sm font-medium transition-colors inline-flex items-center justify-center gap-1.5
          ${saved
            ? 'bg-green-50 border border-green-200 text-green-700'
            : 'bg-foreground text-background hover:bg-zinc-700'}`}
      >
        {saved ? <><Check className="w-4 h-4" /> Đã lưu</> : 'Lưu cài đặt'}
      </button>
    </div>
  );
}
