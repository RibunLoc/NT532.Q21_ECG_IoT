'use client';
import { getLabelStyle } from '@/lib/labelColors';

export default function ClassBadge({ label }: { label: string }) {
  const s = getLabelStyle(label);
  return (
    <span className={`inline-flex items-center gap-1.5 px-2.5 py-1 rounded-md border text-xs font-medium ${s.badge}`}>
      {s.abnormal && <span className="w-1.5 h-1.5 rounded-full" style={{ background: s.hex }} />}
      {s.vi}
    </span>
  );
}
