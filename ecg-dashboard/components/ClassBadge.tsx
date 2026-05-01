'use client';

const CONFIG: Record<string, { bg: string; text: string; label: string }> = {
  Normal:           { bg: 'bg-green-900',  text: 'text-green-300',  label: 'Bình thường' },
  Ventricular:      { bg: 'bg-red-900',    text: 'text-red-300',    label: 'Rung thất (VEB)' },
  Supraventricular: { bg: 'bg-amber-900',  text: 'text-amber-300',  label: 'Trên thất (SVE)' },
  Fusion:           { bg: 'bg-blue-900',   text: 'text-blue-300',   label: 'Nhịp hỗn hợp' },
  Unknown:          { bg: 'bg-gray-700',   text: 'text-gray-300',   label: 'Không xác định' },
  Leads_Off:        { bg: 'bg-gray-800',   text: 'text-gray-400',   label: 'Mất tín hiệu' },
};

export default function ClassBadge({ label }: { label: string }) {
  const cfg = CONFIG[label] ?? CONFIG['Unknown'];
  return (
    <span className={`inline-block px-3 py-1 rounded-full text-xs font-semibold ${cfg.bg} ${cfg.text}`}>
      {cfg.label}
    </span>
  );
}
