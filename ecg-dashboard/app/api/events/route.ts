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
    // Query by device_id (partition key), sort by timestamp descending
    const cmd = new QueryCommand({
      TableName:                TABLE,
      KeyConditionExpression:   'device_id = :d',
      ExpressionAttributeValues: { ':d': { S: device } },
      ScanIndexForward:         false,
      Limit:                    limit,
      ...(alertOnly && {
        FilterExpression:            'is_alert = :t',
        ExpressionAttributeValues:   { ':d': { S: device }, ':t': { BOOL: true } },
      }),
    });

    const result = await ddb.send(cmd);
    const items  = (result.Items ?? []).map(i => unmarshall(i));
    return NextResponse.json({ items });
  } catch (err) {
    console.error('[api/events]', err);
    return NextResponse.json({ error: String(err) }, { status: 500 });
  }
}
