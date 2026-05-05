import { NextRequest, NextResponse } from 'next/server';
import { IoTClient, AttachPolicyCommand, ListAttachedPoliciesCommand } from '@aws-sdk/client-iot';

const iot = new IoTClient({ region: 'ap-southeast-1' });
const POLICY_NAME = 'ECG-Dashboard-Cognito-Policy';

export async function POST(req: NextRequest) {
  try {
    const { identityId } = await req.json();
    if (!identityId || !identityId.startsWith('ap-southeast-1:')) {
      return NextResponse.json({ error: 'invalid identityId' }, { status: 400 });
    }

    // Kiểm tra đã attach chưa để tránh gọi thừa
    const existing = await iot.send(new ListAttachedPoliciesCommand({ target: identityId }));
    const alreadyAttached = existing.policies?.some(p => p.policyName === POLICY_NAME);
    if (alreadyAttached) {
      return NextResponse.json({ status: 'already_attached' });
    }

    await iot.send(new AttachPolicyCommand({ policyName: POLICY_NAME, target: identityId }));
    return NextResponse.json({ status: 'attached' });
  } catch (err) {
    console.error('[iot-attach]', err);
    return NextResponse.json({ error: String(err) }, { status: 500 });
  }
}
