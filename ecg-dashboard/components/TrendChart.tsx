'use client';
import { LineChart, Line, XAxis, YAxis, Tooltip, ResponsiveContainer, CartesianGrid, Legend } from 'recharts';

export interface TrendPoint {
  t:     number;   // epoch ms (để sort)
  time:  string;   // nhãn hiển thị (HH:mm)
  hr?:   number;   // nhịp tim PPG
  spo2?: number;   // SpO2 %
}

const HR_COLOR   = '#dc2626';   // đỏ — nhịp tim
const SPO2_COLOR = '#2563eb';   // xanh dương — oxy máu

export default function TrendChart({ data }: { data: TrendPoint[] }) {
  if (data.length === 0) {
    return (
      <div className="h-48 flex items-center justify-center text-faint text-sm">
        Chưa có dữ liệu SpO₂ / nhịp tim.
      </div>
    );
  }

  return (
    <ResponsiveContainer width="100%" height={220}>
      <LineChart data={data} margin={{ top: 8, right: 8, bottom: 0, left: -16 }}>
        <CartesianGrid stroke="#f4f4f5" vertical={false} />
        <XAxis dataKey="time" tick={{ fill: '#71717a', fontSize: 11 }} axisLine={{ stroke: '#ececec' }} tickLine={false} minTickGap={32} />
        <YAxis
          yAxisId="hr"
          domain={[40, 160]}
          tick={{ fill: '#71717a', fontSize: 11 }} axisLine={false} tickLine={false}
          width={36}
        />
        <YAxis
          yAxisId="spo2"
          orientation="right"
          domain={[80, 100]}
          tick={{ fill: '#71717a', fontSize: 11 }} axisLine={false} tickLine={false}
          width={36}
        />
        <Tooltip
          contentStyle={{ background: '#ffffff', border: '1px solid #ececec', borderRadius: 8, fontSize: 12 }}
          labelStyle={{ color: '#18181b' }}
        />
        <Legend wrapperStyle={{ fontSize: 12 }} iconType="plainline" />
        <Line yAxisId="hr"   type="monotone" dataKey="hr"   name="Nhịp tim (BPM)" stroke={HR_COLOR}   strokeWidth={1.75} dot={false} connectNulls isAnimationActive={false} />
        <Line yAxisId="spo2" type="monotone" dataKey="spo2" name="SpO₂ (%)"        stroke={SPO2_COLOR} strokeWidth={1.75} dot={false} connectNulls isAnimationActive={false} />
      </LineChart>
    </ResponsiveContainer>
  );
}
