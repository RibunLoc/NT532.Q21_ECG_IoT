'use client';
import { useState } from 'react';

export default function SettingsPage() {
  const [vebThreshold,  setVebThreshold]  = useState('0.4');
  const [edgeThreshold, setEdgeThreshold] = useState('0.8');
  const [recipient,     setRecipient]     = useState('hothanhloc12345@gmail.com');
  const [saved,         setSaved]         = useState(false);

  const handleSave = () => {
    // TODO: gọi API để update Lambda env vars
    setSaved(true);
    setTimeout(() => setSaved(false), 2000);
  };

  return (
    <div className="max-w-xl mx-auto space-y-6">
      <div>
        <h1 className="text-xl font-bold">Cài đặt</h1>
        <p className="text-sm text-gray-500">Ngưỡng cảnh báo và thông báo</p>
      </div>

      <div className="bg-gray-900 rounded-xl border border-gray-800 p-5 space-y-5">
        <p className="text-sm font-semibold text-gray-300">Ngưỡng phân loại</p>

        <div>
          <label className="text-xs text-gray-400 block mb-1">
            Cloud VEB Threshold (hiện tại: {vebThreshold})
          </label>
          <input
            type="range" min="0.2" max="0.8" step="0.05"
            value={vebThreshold}
            onChange={e => setVebThreshold(e.target.value)}
            className="w-full accent-red-500"
          />
          <p className="text-xs text-gray-600 mt-1">
            Thấp hơn → nhạy hơn với VEB, nhiều false positive hơn
          </p>
        </div>

        <div>
          <label className="text-xs text-gray-400 block mb-1">
            Edge Confidence Threshold (hiện tại: {edgeThreshold})
          </label>
          <input
            type="range" min="0.5" max="0.95" step="0.05"
            value={edgeThreshold}
            onChange={e => setEdgeThreshold(e.target.value)}
            className="w-full accent-blue-500"
          />
          <p className="text-xs text-gray-600 mt-1">
            Cao hơn → edge chắc chắn hơn mới không gửi cloud
          </p>
        </div>
      </div>

      <div className="bg-gray-900 rounded-xl border border-gray-800 p-5 space-y-4">
        <p className="text-sm font-semibold text-gray-300">Thông báo email</p>

        <div>
          <label className="text-xs text-gray-400 block mb-1">Email nhận cảnh báo</label>
          <input
            type="email"
            value={recipient}
            onChange={e => setRecipient(e.target.value)}
            className="w-full bg-gray-800 border border-gray-700 rounded-lg px-3 py-2
                       text-sm text-gray-200 focus:outline-none focus:border-blue-500"
          />
        </div>
      </div>

      <button
        onClick={handleSave}
        className={`w-full py-2 rounded-lg text-sm font-medium transition-colors
          ${saved
            ? 'bg-green-800 text-green-300'
            : 'bg-blue-700 hover:bg-blue-600 text-white'}`}
      >
        {saved ? '✓ Đã lưu' : 'Lưu cài đặt'}
      </button>
    </div>
  );
}
