'use client';
import { LABEL_STYLE } from '@/lib/labelColors';

// Thứ tự probs từ model: [N, S, V, F, Q]
const ORDER = ['Normal', 'Supraventricular', 'Ventricular', 'Fusion', 'Unknown'];

interface Props {
  probs: number[];
}

export default function ProbBar({ probs }: Props) {
  return (
    <div className="space-y-2.5">
      {ORDER.map((key, i) => {
        const s = LABEL_STYLE[key];
        const pct = Math.round((probs[i] ?? 0) * 100);
        return (
          <div key={key} className="flex items-center gap-3">
            <span className="text-xs text-muted w-32 shrink-0">{s.vi}</span>
            <div className="flex-1 h-1.5 bg-zinc-100 rounded-full overflow-hidden">
              <div
                className="h-full rounded-full transition-all duration-300"
                style={{ width: `${pct}%`, background: s.hex }}
              />
            </div>
            <span className="text-xs font-mono w-9 text-right tabular-nums text-muted">
              {pct}%
            </span>
          </div>
        );
      })}
    </div>
  );
}
