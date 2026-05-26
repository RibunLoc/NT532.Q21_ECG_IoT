'use client';
import Link from 'next/link';
import { usePathname } from 'next/navigation';
import { signOut } from 'aws-amplify/auth';
import { LayoutDashboard, LineChart, Bell, Settings, LogOut, Activity } from 'lucide-react';

const NAV = [
  { href: '/',         label: 'Dashboard', icon: LayoutDashboard },
  { href: '/history',  label: 'Lịch sử',   icon: LineChart },
  { href: '/alerts',   label: 'Cảnh báo',  icon: Bell },
  { href: '/settings', label: 'Cài đặt',   icon: Settings },
];

export default function Sidebar() {
  const path = usePathname();
  return (
    <aside className="w-56 shrink-0 bg-surface border-r border-border flex flex-col">
      {/* Logo */}
      <div className="px-5 h-16 flex items-center gap-2 border-b border-border">
        <Activity className="w-4 h-4 text-foreground" strokeWidth={2} />
        <span className="text-sm font-semibold tracking-tight">ECG Monitor</span>
      </div>

      {/* Nav */}
      <nav className="flex-1 px-3 py-4 space-y-0.5">
        {NAV.map(n => {
          const active = path === n.href;
          const Icon = n.icon;
          return (
            <Link
              key={n.href}
              href={n.href}
              className={`flex items-center gap-3 px-3 py-2 rounded-md text-sm transition-colors
                ${active
                  ? 'bg-zinc-100 text-foreground font-medium'
                  : 'text-muted hover:text-foreground hover:bg-zinc-50'}`}
            >
              <Icon className="w-4 h-4 shrink-0" strokeWidth={1.75} />
              {n.label}
            </Link>
          );
        })}
      </nav>

      {/* Sign out */}
      <div className="px-3 pb-4">
        <button
          onClick={() => signOut()}
          className="w-full flex items-center gap-3 px-3 py-2 rounded-md text-sm
                     text-faint hover:text-foreground hover:bg-zinc-50 transition-colors"
        >
          <LogOut className="w-4 h-4 shrink-0" strokeWidth={1.75} />
          Đăng xuất
        </button>
      </div>
    </aside>
  );
}
