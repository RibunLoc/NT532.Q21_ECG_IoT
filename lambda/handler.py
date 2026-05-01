"""
ECG Cloud Inference Lambda — Cascade verification

Trigger: IoT Rule subscribe topic 'ecg/raw' (ESP32 publish raw waveform khi
         needs_cloud_check = true).

Flow:
  1. Run ONNX inference tren 187 samples
  2. Luu ket qua vao DynamoDB (audit log)
  3. Neu cloud confirm label != Normal va trung khop voi edge → publish 'ecg/alert'
     → IoT Rule 'ECG_Alert_SNS' (subscribe 'ecg/alert') se trigger SNS email
  4. Neu cloud predict Normal (edge nham) → KHONG publish → KHONG email
"""
import os
import time
import json
from datetime import datetime, timezone

import boto3
import numpy as np
import onnxruntime as ort

# ── Config
MODEL_PATH     = '/var/task/ecg_cloud_model.onnx'
DYNAMODB_TABLE = os.environ.get('DDB_TABLE', 'ecg-events')
VEB_THRESHOLD  = float(os.environ.get('VEB_THRESHOLD', '0.4'))
ALERT_TOPIC    = os.environ.get('ALERT_TOPIC', 'ecg/alert')

AAMI_LABELS    = ['N', 'S', 'V', 'F', 'Q']
LABEL_FULLNAMES = {
    'N': 'Normal',
    'S': 'Supraventricular',
    'V': 'Ventricular',
    'F': 'Fusion',
    'Q': 'Unknown',
}

# ── Init 1 lan khi cold start
print('Loading ONNX model...')
session    = ort.InferenceSession(MODEL_PATH, providers=['CPUExecutionProvider'])
input_name = session.get_inputs()[0].name
print(f'Model loaded. Input: {input_name}')

ddb     = boto3.resource('dynamodb').Table(DYNAMODB_TABLE)
iot_data = boto3.client('iot-data')


def lambda_handler(event, _context):
    """
    event payload tu IoT Rule (topic 'ecg/raw'):
    {
      "device_id": "ecg-device-001",
      "timestamp": 1745923456,
      "samples": [187 floats]
    }

    Hoac payload tu ESP32 result message kem theo:
    {
      "device_id": ..., "timestamp": ...,
      "edge_label": "Ventricular", "edge_confidence": 0.65,
      "samples": [187 floats]
    }
    """
    try:
        device_id  = event['device_id']
        ts         = int(event.get('timestamp', time.time()))
        edge_label = event.get('edge_label', 'unknown')
        edge_conf  = float(event.get('edge_confidence', 0.0))
        samples    = event['samples']

        if len(samples) != 187:
            return {'status': 'error', 'msg': f'expected 187 samples, got {len(samples)}'}

        # ── ONNX inference
        x = np.array(samples, dtype=np.float32).reshape(1, 187, 1)
        proba = session.run(None, {input_name: x})[0][0]   # shape (5,)

        # ── Threshold tuning cho VEB
        cloud_pred = int(np.argmax(proba))
        if proba[2] > VEB_THRESHOLD:
            cloud_pred = 2
        cloud_short = AAMI_LABELS[cloud_pred]                  # 'V'
        cloud_label = LABEL_FULLNAMES[cloud_short]             # 'Ventricular'
        cloud_conf  = float(proba[cloud_pred])

        # ── Cascade decision: alert chi khi CA edge VA cloud deu thay abnormal
        edge_abnormal  = edge_label not in ('Normal', 'unknown', 'Leads_Off')
        cloud_abnormal = cloud_short != 'N'
        is_alert       = edge_abnormal and cloud_abnormal

        iso_ts = datetime.fromtimestamp(ts, tz=timezone.utc).isoformat()

        # ── Luu DynamoDB (audit log moi inference, ke ca khong alert)
        ddb.put_item(Item={
            'device_id':        device_id,
            'timestamp':        ts,
            'iso_timestamp':    iso_ts,
            'edge_label':       edge_label,
            'edge_confidence':  str(round(edge_conf, 4)),
            'cloud_label':      cloud_label,
            'cloud_confidence': str(round(cloud_conf, 4)),
            'cloud_proba':      [str(round(float(p), 4)) for p in proba],
            'is_alert':         is_alert,
            'source':           'cloud-verified',
        })

        # ── Cloud confirm bat thuong → publish ecg/alert → IoT Rule trigger SNS
        if is_alert:
            alert_payload = {
                'device_id':        device_id,
                'timestamp':        ts,
                'iso_timestamp':    iso_ts,
                'edge_label':       edge_label,
                'edge_confidence':  round(edge_conf, 4),
                'cloud_label':      cloud_label,
                'cloud_confidence': round(cloud_conf, 4),
                'probabilities':    {l: round(float(p), 4) for l, p in zip(AAMI_LABELS, proba)},
                'severity':         'high' if cloud_short == 'V' else 'medium',
                'recommendation':   'Kiem tra benh nhan — ca edge va cloud cung phat hien bat thuong.',
            }
            iot_data.publish(
                topic=ALERT_TOPIC,
                qos=1,
                payload=json.dumps(alert_payload),
            )
            print(f'ALERT published to {ALERT_TOPIC} for {device_id}: edge={edge_label} cloud={cloud_label}')
        else:
            print(f'No alert: edge={edge_label} cloud={cloud_label} (filtered false alarm)')

        return {
            'status':       'ok',
            'device_id':    device_id,
            'edge_label':   edge_label,
            'cloud_label':  cloud_label,
            'cloud_conf':   round(cloud_conf, 4),
            'is_alert':     is_alert,
        }

    except KeyError as e:
        print(f'Missing field: {e}')
        return {'status': 'error', 'msg': f'missing field {e}'}
    except Exception as e:
        print(f'Error: {e}')
        return {'status': 'error', 'msg': str(e)}
