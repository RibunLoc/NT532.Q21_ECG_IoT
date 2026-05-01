import { NextRequest, NextResponse } from 'next/server';
import { DynamoDBClient, QueryCommand } from '@aws-sdk/client-dynamodb';
import { unmarshall } from '@aws-sdk/util-dynamodb';
import Anthropic from '@anthropic-ai/sdk';

const ddb = new DynamoDBClient({ region: 'ap-southeast-1' });
const TABLE = 'ecg-events';

const SYSTEM_PROMPT = `Bạn là chuyên gia phân tích sức khỏe tim mạch. Nhiệm vụ của bạn là phân tích dữ liệu ECG thu thập từ thiết bị IoT đeo tay và tạo báo cáo tổng hợp bằng tiếng Việt.

Phân loại nhịp tim theo chuẩn AAMI:
- Normal (N): Nhịp xoang bình thường
- Supraventricular (S/SVE): Nhịp trên thất — thường lành tính nhưng cần theo dõi
- Ventricular (V/VEB): Nhịp thất bất thường — nguy hiểm, cần chú ý đặc biệt
- Fusion (F): Nhịp hỗn hợp — cần đánh giá thêm
- Unknown (Q): Không phân loại được

Khi viết báo cáo:
1. Dùng ngôn ngữ thân thiện, dễ hiểu cho người dùng thông thường (không phải bác sĩ)
2. Trình bày rõ ràng tỷ lệ các loại nhịp tim
3. Đánh giá mức độ rủi ro dựa trên số lần cảnh báo và loại nhịp bất thường
4. Đưa ra khuyến nghị cụ thể
5. Nhấn mạnh: đây là hỗ trợ tham khảo, không thay thế chẩn đoán của bác sĩ

Định dạng báo cáo gồm các phần:
- Tổng quan sức khỏe tim mạch (1 đoạn tóm tắt)
- Phân tích chi tiết từng loại nhịp
- Đánh giá rủi ro (Thấp / Trung bình / Cao)
- Khuyến nghị hành động`;

export async function GET(req: NextRequest) {
  const { searchParams } = new URL(req.url);
  const device = searchParams.get('device') ?? 'ecg-device-001';
  const limit  = parseInt(searchParams.get('limit') ?? '200');

  // 1. Query DynamoDB
  let events: Record<string, unknown>[] = [];
  try {
    const cmd = new QueryCommand({
      TableName:                TABLE,
      KeyConditionExpression:   'device_id = :d',
      ExpressionAttributeValues: { ':d': { S: device } },
      ScanIndexForward:         false,
      Limit:                    limit,
    });
    const result = await ddb.send(cmd);
    events = (result.Items ?? []).map(i => unmarshall(i));
  } catch (err) {
    return NextResponse.json({ error: `DynamoDB error: ${err}` }, { status: 500 });
  }

  if (events.length === 0) {
    return NextResponse.json({ error: 'Không có dữ liệu ECG để phân tích.' }, { status: 404 });
  }

  // 2. Aggregate statistics
  const counts: Record<string, number> = {};
  let alertCount = 0;
  const recentAlerts: string[] = [];

  for (const e of events) {
    const label = (e.cloud_label as string) || (e.edge_label as string) || 'Unknown';
    counts[label] = (counts[label] ?? 0) + 1;
    if (e.is_alert) {
      alertCount++;
      if (recentAlerts.length < 5) {
        const ts = e.iso_timestamp
          ? new Date(e.iso_timestamp as string).toLocaleString('vi-VN', { timeZone: 'Asia/Ho_Chi_Minh' })
          : '—';
        recentAlerts.push(`${ts}: ${label} (cloud ${Math.round(parseFloat((e.cloud_confidence as string) ?? '0') * 100)}%)`);
      }
    }
  }

  const total = events.length;
  const oldestTs = events[events.length - 1]?.iso_timestamp;
  const newestTs = events[0]?.iso_timestamp;
  const timeRange = oldestTs && newestTs
    ? `${new Date(oldestTs as string).toLocaleString('vi-VN', { timeZone: 'Asia/Ho_Chi_Minh' })} đến ${new Date(newestTs as string).toLocaleString('vi-VN', { timeZone: 'Asia/Ho_Chi_Minh' })}`
    : 'Không rõ';

  const statsText = Object.entries(counts)
    .map(([label, count]) => `  - ${label}: ${count} nhịp (${((count / total) * 100).toFixed(1)}%)`)
    .join('\n');

  const userMessage = `Phân tích dữ liệu ECG của thiết bị ${device}:

Khoảng thời gian: ${timeRange}
Tổng số nhịp tim ghi nhận: ${total}
Số cảnh báo bất thường: ${alertCount} (${((alertCount / total) * 100).toFixed(1)}%)

Phân bố loại nhịp:
${statsText}

${recentAlerts.length > 0 ? `Cảnh báo gần nhất (${recentAlerts.length} trong số ${alertCount}):\n${recentAlerts.map(a => `  - ${a}`).join('\n')}` : 'Không có cảnh báo bất thường.'}

Hãy tạo báo cáo sức khỏe tim mạch đầy đủ theo định dạng đã hướng dẫn.`;

  // 3. Stream Claude response
  const client = new Anthropic();

  const encoder = new TextEncoder();
  const stream = new ReadableStream({
    async start(controller) {
      try {
        const response = await client.messages.create({
          model:      'claude-opus-4-7',
          max_tokens: 2048,
          thinking:   { type: 'adaptive' },
          system: [
            {
              type: 'text',
              text: SYSTEM_PROMPT,
              cache_control: { type: 'ephemeral' },
            },
          ],
          messages: [{ role: 'user', content: userMessage }],
          stream: true,
        });

        for await (const event of response) {
          if (
            event.type === 'content_block_delta' &&
            event.delta.type === 'text_delta'
          ) {
            controller.enqueue(encoder.encode(event.delta.text));
          }
        }
      } catch (err) {
        controller.enqueue(encoder.encode(`\n\n[Lỗi: ${err}]`));
      } finally {
        controller.close();
      }
    },
  });

  return new Response(stream, {
    headers: {
      'Content-Type':      'text/plain; charset=utf-8',
      'X-Accel-Buffering': 'no',
      'Cache-Control':     'no-cache',
      'X-Stats-Total':     String(total),
      'X-Stats-Alerts':    String(alertCount),
      'X-Stats-Counts':    JSON.stringify(counts),
    },
  });
}
