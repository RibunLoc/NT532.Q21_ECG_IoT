'use client';
import { LineChart, Line, XAxis, YAxis, ResponsiveContainer, ReferenceLine } from 'recharts';

interface Props {
  samples:   number[];
  label?:    string;
  color?:    string;
}

import { getLabelStyle } from '@/lib/labelColors';

export default function EcgWaveform({ samples, label = 'Normal', color }: Props) {
  // Sóng tô theo màu ngữ nghĩa của nhãn; Normal dùng đen trung tính cho dễ đọc.
  const s = getLabelStyle(label);
  const lineColor = color ?? (label === 'Normal' ? '#18181b' : s.hex);
  const data = samples.map((v, i) => ({ i, v }));

  return (
    <div className="w-full h-40 bg-zinc-50 rounded-lg p-2 border border-border">
      <ResponsiveContainer width="100%" height="100%">
        <LineChart data={data} margin={{ top: 4, right: 4, bottom: 0, left: -20 }}>
          <XAxis dataKey="i" hide />
          <YAxis domain={[0, 1]} hide />
          <ReferenceLine y={0.5} stroke="#e4e4e7" strokeDasharray="3 3" />
          <Line
            type="monotone"
            dataKey="v"
            stroke={lineColor}
            strokeWidth={1.5}
            dot={false}
            isAnimationActive={false}
          />
        </LineChart>
      </ResponsiveContainer>
    </div>
  );
}
