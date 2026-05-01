import type { Metadata } from 'next';
import { Geist, Geist_Mono } from 'next/font/google';
import './globals.css';
import AmplifyProvider from '@/components/AmplifyProvider';
import Sidebar from '@/components/Sidebar';

const geistSans = Geist({ subsets: ['latin'], variable: '--font-geist-sans' });
const geistMono = Geist_Mono({ subsets: ['latin'], variable: '--font-geist-mono' });

export const metadata: Metadata = {
  title: 'ECG Dashboard',
  description: 'Hệ thống giám sát điện tim IoT',
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="vi" className={`${geistSans.variable} ${geistMono.variable} h-full`}>
      <body className="bg-gray-950 text-gray-100 antialiased h-full" suppressHydrationWarning>
        <AmplifyProvider>
          <div className="flex h-screen overflow-hidden">
            <Sidebar />
            <main className="flex-1 overflow-y-auto p-6">
              {children}
            </main>
          </div>
        </AmplifyProvider>
      </body>
    </html>
  );
}
