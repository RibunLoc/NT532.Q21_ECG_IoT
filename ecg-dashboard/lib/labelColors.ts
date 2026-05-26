// Bảng màu NGỮ NGHĨA cho các loại nhịp tim — màu có chức năng phân biệt
// mức độ nghiêm trọng, không phải trang trí:
//   xanh lá = bình thường (an toàn)
//   đỏ      = rung thất VEB (nguy hiểm nhất)
//   hổ phách= trên thất SVE (cảnh báo vừa)
//   tím     = nhịp hỗn hợp Fusion
//   xám     = không xác định / mất tín hiệu
export interface LabelStyle {
  vi: string;       // tên tiếng Việt
  hex: string;      // màu cho chart/recharts
  badge: string;    // class Tailwind cho badge (viền + nền + chữ)
  abnormal: boolean;
}

export const LABEL_STYLE: Record<string, LabelStyle> = {
  Normal:           { vi: 'Bình thường',     hex: '#16a34a', badge: 'border-green-200 bg-green-50 text-green-700',   abnormal: false },
  Ventricular:      { vi: 'Rung thất (VEB)', hex: '#dc2626', badge: 'border-red-200 bg-red-50 text-red-700',         abnormal: true  },
  Supraventricular: { vi: 'Trên thất (SVE)', hex: '#d97706', badge: 'border-amber-200 bg-amber-50 text-amber-700',   abnormal: true  },
  Fusion:           { vi: 'Nhịp hỗn hợp',    hex: '#7c3aed', badge: 'border-violet-200 bg-violet-50 text-violet-700', abnormal: true  },
  Unknown:          { vi: 'Không xác định',  hex: '#a1a1aa', badge: 'border-border bg-zinc-50 text-faint',           abnormal: false },
  Leads_Off:        { vi: 'Mất tín hiệu',    hex: '#a1a1aa', badge: 'border-border bg-zinc-50 text-faint',           abnormal: false },
};

export function getLabelStyle(label: string): LabelStyle {
  return LABEL_STYLE[label] ?? LABEL_STYLE['Unknown'];
}
