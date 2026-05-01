import json
import boto3
import os
from datetime import datetime, timezone, timedelta

# ── Config từ Environment Variables ──────────────────────
SENDER_EMAIL    = os.environ.get('SENDER_EMAIL',    'ecg-notifications@holoc.id.vn')
RECIPIENT_EMAIL = os.environ.get('RECIPIENT_EMAIL', 'hothanhloc12345@gmail.com')
AWS_REGION      = os.environ.get('AWS_SES_REGION',  'ap-southeast-1')

ses = boto3.client('ses', region_name=AWS_REGION)

LABEL_CONFIG = {
    'Ventricular':      {'color': '#A32D2D', 'bg': '#FCEBEB', 'severity': 'Nguy hiểm'},
    'Supraventricular': {'color': '#854F0B', 'bg': '#FAEEDA', 'severity': 'Cảnh báo'},
    'Fusion':           {'color': '#185FA5', 'bg': '#E6F1FB', 'severity': 'Chú ý'},
    'Unknown':          {'color': '#5F5E5A', 'bg': '#F1EFE8', 'severity': 'Chú ý'},
}

LABEL_VI = {
    'Ventricular':      'Rung thất (VEB)',
    'Supraventricular': 'Trên thất (SVE)',
    'Fusion':           'Nhịp hỗn hợp',
    'Unknown':          'Không xác định',
    'Normal':           'Bình thường',
}

def format_timestamp(ts_val):
    """
    timestamp từ ecg/alert là Unix seconds (int từ handler.py).
    Nếu giá trị quá nhỏ (millis() từ ESP32 boot) thì dùng thời gian hiện tại.
    """
    try:
        ts = int(ts_val)
        # millis() từ ESP32 thường < 10^8 ms (~27 giờ boot), Unix epoch > 1.7×10^9
        if ts < 1_000_000_000:
            # Đây là millis() từ ESP32 — không thể convert, dùng thời gian Lambda nhận
            dt = datetime.now(tz=timezone(timedelta(hours=7)))
            return dt.strftime('%d/%m/%Y %H:%M:%S (UTC+7)') + ' *'
        dt = datetime.fromtimestamp(ts, tz=timezone(timedelta(hours=7)))
        return dt.strftime('%d/%m/%Y %H:%M:%S (UTC+7)')
    except:
        return str(ts_val)

def confidence_bar(value, color, bg_color):
    pct = min(100, max(0, float(value) * 100))
    return f'''
    <div style="display:flex; align-items:center; gap:8px; margin-bottom:6px;">
      <div style="flex:1; height:8px; background:{bg_color}; border-radius:4px; overflow:hidden;">
        <div style="width:{pct:.1f}%; height:100%; background:{color}; border-radius:4px;"></div>
      </div>
      <span style="font-size:12px; color:{color}; font-weight:600; min-width:40px; text-align:right;">
        {pct:.1f}%
      </span>
    </div>'''

def build_html_email(data):
    label      = data.get('label', 'Unknown')
    confidence = float(data.get('confidence', 0))
    device_id  = data.get('device_id', 'Unknown')
    timestamp  = data.get('timestamp', 0)
    all_probs  = data.get('all_probs', {})

    cfg      = LABEL_CONFIG.get(label, LABEL_CONFIG['Unknown'])
    color    = cfg['color']
    bg_color = cfg['bg']
    severity = cfg['severity']
    label_vi = LABEL_VI.get(label, label)
    conf_pct = confidence * 100
    time_str = format_timestamp(timestamp)

    labels_order = ['Bình thường', 'Trên thất (SVE)', 'Rung thất (VEB)', 'Nhịp hỗn hợp', 'Không xác định']
    label_keys   = ['Normal', 'Supraventricular', 'Ventricular', 'Fusion', 'Unknown']

    prob_bars = ''
    for key, lbl_vi in zip(label_keys, labels_order):
        prob = float(all_probs.get(key, 0))
        is_detected = (key == label)
        bar_color   = color if is_detected else '#B4B2A9'
        bar_bg      = bg_color if is_detected else '#F1EFE8'
        weight      = '600' if is_detected else '400'
        txt_color   = color if is_detected else '#888780'
        prob_bars  += f'''
        <tr>
          <td style="padding:4px 0; font-size:12px; color:{txt_color};
                     font-weight:{weight}; width:160px;">{lbl_vi}</td>
          <td style="padding:4px 8px;">
            {confidence_bar(prob, bar_color, bar_bg)}
          </td>
        </tr>'''

    html = f'''<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Cảnh báo ECG</title>
</head>
<body style="margin:0; padding:0; background:#F1EFE8; font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;">
<table width="100%" cellpadding="0" cellspacing="0" style="background:#F1EFE8; padding:32px 16px;">
<tr><td align="center">
<table width="560" cellpadding="0" cellspacing="0" style="background:#FFFFFF; border-radius:12px; overflow:hidden; border:0.5px solid #D3D1C7;">

  <!-- Header -->
  <tr>
    <td style="background:{color}; padding:20px 24px;">
      <table width="100%" cellpadding="0" cellspacing="0">
        <tr>
          <td>
            <p style="margin:0; font-size:18px; font-weight:600; color:#FFFFFF;">
              Cảnh báo ECG — {severity}
            </p>
            <p style="margin:4px 0 0; font-size:13px; color:rgba(255,255,255,0.75);">
              Hệ thống giám sát điện tim IoT
            </p>
          </td>
          <td align="right">
            <div style="background:rgba(255,255,255,0.15); border-radius:8px;
                        padding:8px 12px; display:inline-block;">
              <p style="margin:0; font-size:11px; color:rgba(255,255,255,0.9);
                         text-transform:uppercase; letter-spacing:0.05em;">Mức độ</p>
              <p style="margin:2px 0 0; font-size:16px; font-weight:700;
                         color:#FFFFFF;">{severity.upper()}</p>
            </div>
          </td>
        </tr>
      </table>
    </td>
  </tr>

  <!-- Alert Banner -->
  <tr>
    <td style="padding:16px 24px 0;">
      <div style="background:{bg_color}; border-left:3px solid {color};
                  border-radius:0 8px 8px 0; padding:10px 14px;">
        <p style="margin:0; font-size:13px; font-weight:500; color:{color};">
          Phát hiện nhịp tim bất thường trên thiết bị <strong>{device_id}</strong>.
          Độ tin cậy: {conf_pct:.1f}%. Vui lòng kiểm tra ngay.
        </p>
      </div>
    </td>
  </tr>

  <!-- Metric Cards -->
  <tr>
    <td style="padding:16px 24px 0;">
      <table width="100%" cellpadding="0" cellspacing="0">
        <tr>
          <td width="48%" style="background:#F1EFE8; border-radius:8px; padding:12px 14px;">
            <p style="margin:0 0 3px; font-size:11px; color:#888780;
                       text-transform:uppercase; letter-spacing:0.05em;">Phân loại</p>
            <p style="margin:0; font-size:18px; font-weight:700; color:{color};">{label_vi}</p>
          </td>
          <td width="4%"></td>
          <td width="48%" style="background:#F1EFE8; border-radius:8px; padding:12px 14px;">
            <p style="margin:0 0 3px; font-size:11px; color:#888780;
                       text-transform:uppercase; letter-spacing:0.05em;">Độ tin cậy</p>
            <p style="margin:0; font-size:20px; font-weight:700; color:{color};">{conf_pct:.1f}%</p>
          </td>
        </tr>
        <tr><td colspan="3" style="padding:6px 0;"></td></tr>
        <tr>
          <td width="48%" style="background:#F1EFE8; border-radius:8px; padding:12px 14px;">
            <p style="margin:0 0 3px; font-size:11px; color:#888780;
                       text-transform:uppercase; letter-spacing:0.05em;">Thiết bị</p>
            <p style="margin:0; font-size:14px; font-weight:600;
                       color:#2C2C2A; font-family:monospace;">{device_id}</p>
          </td>
          <td width="4%"></td>
          <td width="48%" style="background:#F1EFE8; border-radius:8px; padding:12px 14px;">
            <p style="margin:0 0 3px; font-size:11px; color:#888780;
                       text-transform:uppercase; letter-spacing:0.05em;">Thời gian</p>
            <p style="margin:0; font-size:12px; font-weight:600; color:#2C2C2A;">{time_str}</p>
          </td>
        </tr>
      </table>
    </td>
  </tr>

  <!-- Probability Bars -->
  <tr>
    <td style="padding:16px 24px 0;">
      <p style="margin:0 0 10px; font-size:12px; font-weight:600;
                 color:#888780; text-transform:uppercase; letter-spacing:0.05em;">
        Xác suất từng loại nhịp tim
      </p>
      <table width="100%" cellpadding="0" cellspacing="0">
        {prob_bars}
      </table>
    </td>
  </tr>

  <!-- Edge vs Cloud -->
  <tr>
    <td style="padding:12px 24px 0;">
      <div style="background:#F1EFE8; border-radius:8px; padding:10px 14px;">
        <p style="margin:0 0 4px; font-size:11px; color:#888780;
                   text-transform:uppercase; letter-spacing:0.05em;">Xác nhận kép (Edge + Cloud)</p>
        <p style="margin:0; font-size:12px; color:#2C2C2A; line-height:1.6;">
          Edge: <strong>{data.get('edge_label', '—')}</strong>
          ({float(data.get('edge_confidence', 0))*100:.1f}%)
          &nbsp;·&nbsp;
          Cloud: <strong>{label_vi}</strong>
          ({conf_pct:.1f}%)
        </p>
      </div>
    </td>
  </tr>

  <!-- Footer -->
  <tr>
    <td style="padding:16px 24px 20px;">
      <div style="border-top:0.5px solid #D3D1C7; padding-top:14px;">
        <p style="margin:0; font-size:11px; color:#B4B2A9; line-height:1.6;">
          Cảnh báo này được tạo tự động bởi Hệ thống giám sát điện tim IoT.
          <strong>Đây không phải chẩn đoán y tế.</strong>
          Vui lòng tham khảo ý kiến bác sĩ hoặc chuyên gia y tế có chuyên môn.
        </p>
      </div>
    </td>
  </tr>

</table>
</td></tr>
</table>
</body>
</html>'''
    return html

def build_plain_text(data):
    label      = data.get('label', 'Unknown')
    confidence = float(data.get('confidence', 0)) * 100
    device_id  = data.get('device_id', 'Unknown')
    timestamp  = data.get('timestamp', 0)
    cfg        = LABEL_CONFIG.get(label, LABEL_CONFIG['Unknown'])
    label_vi   = LABEL_VI.get(label, label)
    return f"""CẢNH BÁO ECG — {cfg['severity'].upper()}
=====================================
Phân loại   : {label_vi}
Độ tin cậy  : {confidence:.1f}%
Thiết bị    : {device_id}
Thời gian   : {format_timestamp(timestamp)}
Edge phát hiện : {data.get('edge_label', '—')}
-------------------------------------
Phát hiện nhịp tim bất thường.
ĐÂY KHÔNG PHẢI CHẨN ĐOÁN Y TẾ.
Vui lòng tham khảo ý kiến bác sĩ.
"""

# ── Lambda Handler ────────────────────────────────────────
def lambda_handler(event, context):
    print(f"Event received: {json.dumps(event)}")

    try:
        data = event

        AAMI_TO_FULL = {
            'N': 'Normal', 'S': 'Supraventricular',
            'V': 'Ventricular', 'F': 'Fusion', 'Q': 'Unknown'
        }

        cloud_label = data.get('cloud_label', 'Normal')
        if cloud_label == 'Normal':
            print("Normal rhythm — no alert sent")
            return {'statusCode': 200, 'body': 'Normal — skipped'}

        raw_probs = data.get('probabilities', {})
        all_probs = {AAMI_TO_FULL.get(k, k): v for k, v in raw_probs.items()}

        normalized_data = {
            'label':           cloud_label,
            'confidence':      data.get('cloud_confidence', 0),
            'device_id':       data.get('device_id', 'Unknown'),
            'timestamp':       data.get('timestamp', 0),
            'all_probs':       all_probs,
            'edge_label':      data.get('edge_label', '—'),
            'edge_confidence': data.get('edge_confidence', 0),
        }

        label_vi   = LABEL_VI.get(cloud_label, cloud_label)
        html_body  = build_html_email(normalized_data)
        plain_body = build_plain_text(normalized_data)
        subject    = f"[Cảnh báo ECG] {label_vi} — {data.get('device_id', 'device')}"

        response = ses.send_email(
            Source=SENDER_EMAIL,
            Destination={'ToAddresses': [RECIPIENT_EMAIL]},
            Message={
                'Subject': {'Data': subject, 'Charset': 'UTF-8'},
                'Body': {
                    'Html': {'Data': html_body,  'Charset': 'UTF-8'},
                    'Text': {'Data': plain_body, 'Charset': 'UTF-8'},
                }
            }
        )

        print(f"Email sent! MessageId: {response['MessageId']}")
        return {
            'statusCode': 200,
            'body': json.dumps({'message': 'Alert sent', 'messageId': response['MessageId']})
        }

    except Exception as e:
        print(f"Error: {str(e)}")
        raise e
