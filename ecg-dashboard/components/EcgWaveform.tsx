'use client';
import { LineChart, Line, XAxis, YAxis, ResponsiveContainer, ReferenceLine } from 'recharts';

interface Props {
  samples:   number[];
  label?:    string;
  color?:    string;
}

const LABEL_COLOR: Record<string, string> = {
  Ventricular:      '#A32D2D',
  Supraventricular: '#854F0B',
  Fusion:           '#185FA5',
  Normal:           '#16a34a',
  Unknown:          '#6b7280',
};

export default function EcgWaveform({ samples, label = 'Normal', color }: Props) {
  const lineColor = color ?? LABEL_COLOR[label] ?? '#16a34a';

  const data = samples.map((v, i) => ({ i, v }));

  return (
    <div className="w-full h-40 bg-gray-950 rounded-lg p-2">
      <ResponsiveContainer width="100%" height="100%">
        <LineChart data={data} margin={{ top: 4, right: 4, bottom: 0, left: -20 }}>
          <XAxis dataKey="i" hide />
          <YAxis domain={[0, 1]} hide />
          <ReferenceLine y={0.5} stroke="#374151" strokeDasharray="3 3" />
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
