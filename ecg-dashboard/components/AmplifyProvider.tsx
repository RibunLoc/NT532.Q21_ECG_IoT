'use client';
import { useEffect } from 'react';
import '@/lib/amplify-config';
import { Authenticator } from '@aws-amplify/ui-react';
import '@aws-amplify/ui-react/styles.css';

export default function AmplifyProvider({ children }: { children: React.ReactNode }) {
  return (
    <Authenticator>
      {() => <>{children}</>}
    </Authenticator>
  );
}
