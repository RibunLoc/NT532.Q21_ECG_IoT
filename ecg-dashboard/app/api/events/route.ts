import { NextRequest, NextResponse } from 'next/server';
import { DynamoDBClient, QueryCommand, ScanCommand } from '@aws-sdk/client-dynamodb';
import { unmarshall } from '@aws-sdk/util-dynamodb';

const ddb = new DynamoDBClient({ region: 'ap-southeast-1' });
const TABLE = 'ecg-events';

export async function GET(req: NextRequest) {
  const { searchParams } = new URL(req.url);
  const device    = searchParams.get('device') ?? 'ecg-device-001';
  const alertOnly = searchParams.get('alert') === 'true';
  const limit     = parseInt(searchParams.get('limit') ?? '50');

  try {
    // Query by device_id, sort by timestamp descending.
    // LUU Y: DynamoDB Limit ap dung TRUOC FilterExpression -> neu chi
    // alertOnly, can quet nhieu hon (Limit lon) de filter ra du item.
    // Voi alert: quet toi 500 item gan nhat, filter is_alert=true, lay 'limit' dau.
    const scanLimit = alertOnly ? 500 : limit;
    const cmd = new QueryCommand({
      TableName:                TABLE,
      KeyConditionExpression:   'device_id = :d',
      ExpressionAttributeValues: alertOnly
        ? { ':d': { S: device }, ':t': { BOOL: true } }
        : { ':d': { S: device } },
      ScanIndexForward:         false,
      Limit:                    scanLimit,
      ...(alertOnly && { FilterExpression: 'is_alert = :t' }),
    });

    const result = await ddb.send(cmd);
    const items  = (result.Items ?? []).map(i => unmarshall(i)).slice(0, limit);
    return NextResponse.json({ items });
  } catch (err) {
    console.error('[api/events]', err);
    return NextResponse.json({ error: String(err) }, { status: 500 });
  }
}
