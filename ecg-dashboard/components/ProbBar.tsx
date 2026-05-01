'use client';

const LABELS = [
  { key: 'N', name: 'Bình thường',       color: '#16a34a' },
  { key: 'S', name: 'Trên thất (SVE)',   color: '#854F0B' },
  { key: 'V', name: 'Rung thất (VEB)',   color: '#A32D2D' },
  { key: 'F', name: 'Nhịp hỗn hợp',     color: '#185FA5' },
  { key: 'Q', name: 'Không xác định',   color: '#6b7280' },
];

interface Props {
  probs: number[];   // [N, S, V, F, Q]
}

export default function ProbBar({ probs }: Props) {
  return (
    <div className="space-y-2">
      {LABELS.map((l, i) => {
        const pct = Math.round((probs[i] ?? 0) * 100);
        return (
          <div key={l.key} className="flex items-center gap-2">
            <span className="text-xs text-gray-400 w-32 shrink-0">{l.name}</span>
            <div className="flex-1 h-2 bg-gray-800 rounded-full overflow-hidden">
              <div
                className="h-full rounded-full transition-all duration-300"
                style={{ width: `${pct}%`, background: l.color }}
              />
            </div>
            <span className="text-xs font-mono w-10 text-right" style={{ color: l.color }}>
              {pct}%
            </span>
          </div>
        );
      })}
    </div>
  );
}
