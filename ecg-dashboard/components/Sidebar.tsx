'use client';
import Link from 'next/link';
import { usePathname } from 'next/navigation';
import { signOut } from 'aws-amplify/auth';

const NAV = [
  { href: '/',         label: 'Dashboard',     icon: '📊' },
  { href: '/history',  label: 'Lịch sử',       icon: '📈' },
  { href: '/alerts',   label: 'Cảnh báo',      icon: '🔔' },
  // { href: '/report',   label: 'Báo cáo AI',    icon: '🤖' },
  { href: '/settings', label: 'Cài đặt',       icon: '⚙️' },
];

export default function Sidebar() {
  const path = usePathname();
  return (
    <aside className="w-56 shrink-0 bg-gray-900 border-r border-gray-800 flex flex-col">
      {/* Logo */}
      <div className="px-4 py-5 border-b border-gray-800">
        <p className="text-sm font-bold text-white">❤️ ECG Monitor</p>
        <p className="text-xs text-gray-500 mt-0.5">IoT Monitoring System</p>
      </div>

      {/* Nav */}
      <nav className="flex-1 px-2 py-4 space-y-1">
        {NAV.map(n => {
          const active = path === n.href;
          return (
            <Link
              key={n.href}
              href={n.href}
              className={`flex items-center gap-3 px-3 py-2 rounded-lg text-sm transition-colors
                ${active
                  ? 'bg-red-900/40 text-red-300 font-medium'
                  : 'text-gray-400 hover:bg-gray-800 hover:text-gray-200'}`}
            >
              <span>{n.icon}</span>
              {n.label}
            </Link>
          );
        })}
      </nav>

      {/* Sign out */}
      <div className="px-2 pb-4">
        <button
          onClick={() => signOut()}
          className="w-full flex items-center gap-3 px-3 py-2 rounded-lg text-sm
                     text-gray-500 hover:bg-gray-800 hover:text-gray-300 transition-colors"
        >
          <span>🚪</span> Đăng xuất
        </button>
      </div>
    </aside>
  );
}
