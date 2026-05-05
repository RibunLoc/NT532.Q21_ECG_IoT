'use client';
import { useEffect, useRef, useState, useCallback } from 'react';
import { fetchAuthSession } from 'aws-amplify/auth';
import mqtt, { MqttClient } from 'mqtt';
import { IOT_ENDPOINT, AWS_REGION } from './amplify-config';

export interface EcgResult {
  device_id:         string;
  timestamp:         number;
  label:             string;
  label_index:       number;
  confidence:        number;
  leads_on:          boolean;
  needs_cloud_check: boolean;
  probs:             number[];
}

export interface EcgAlert {
  device_id:        string;
  timestamp:        number;
  iso_timestamp:    string;
  edge_label:       string;
  edge_confidence:  number;
  cloud_label:      string;
  cloud_confidence: number;
  probabilities:    Record<string, number>;
  severity:         string;
}

export interface EcgRaw {
  device_id: string;
  timestamp: number;
  samples:   number[];
}

interface EcgStreamState {
  lastResult:    EcgResult | null;
  lastAlert:     EcgAlert  | null;
  waveform:      number[];           // rolling window for display
  connected:     boolean;            // WebSocket đến AWS
  deviceOnline:  boolean;            // ESP32 có đang gửi data không
  bpm:           number | null;
}

const WAVEFORM_DISPLAY  = 374;    // ~2 beats (47 samples/beat × 2 → hiển thị tốt)
const DEVICE_TIMEOUT_MS = 10_000; // nếu không có ecg/result trong 10s → offline

export function useEcgStream() {
  const clientRef      = useRef<MqttClient | null>(null);
  const lastBeatTs     = useRef<number>(0);
  const lastDeviceMsgTs = useRef<number>(0);

  const [state, setState] = useState<EcgStreamState>({
    lastResult:   null,
    lastAlert:    null,
    waveform:     [],
    connected:    false,
    deviceOnline: false,
    bpm:          null,
  });

  const connect = useCallback(async () => {
    try {
      const session     = await fetchAuthSession();
      const credentials = session.credentials;
      if (!credentials) throw new Error('No credentials');

      const { accessKeyId, secretAccessKey, sessionToken } = credentials;

      // Build AWS SigV4-signed WebSocket URL for IoT Core
      const url = buildWssUrl(IOT_ENDPOINT, AWS_REGION, accessKeyId, secretAccessKey, sessionToken!);

      const client = mqtt.connect(url, {
        protocolVersion: 4,
        reconnectPeriod:  5000,
        keepalive:        30,
      });

      client.on('connect', () => {
        client.subscribe(['ecg/result', 'ecg/raw', 'ecg/alert'], { qos: 1 });
        setState(s => ({ ...s, connected: true }));
      });

      client.on('message', (topic, payload) => {
        try {
          const data = JSON.parse(payload.toString());
          handleMessage(topic, data);
        } catch { /* ignore malformed */ }
      });

      client.on('close', () => setState(s => ({ ...s, connected: false })));
      client.on('error',  (err) => console.error('[MQTT]', err));

      clientRef.current = client;
    } catch (err) {
      console.error('[useEcgStream] connect error:', err);
    }
  }, []);

  function handleMessage(topic: string, data: unknown) {
    if (topic === 'ecg/result') {
      const r = data as EcgResult & { samples?: number[] };
      const now = Date.now();
      const bpm = lastBeatTs.current
        ? Math.round(60000 / (now - lastBeatTs.current))
        : null;
      lastBeatTs.current    = now;
      lastDeviceMsgTs.current = now;

      setState(s => {
        // Nếu ecg/result có samples (downsampled từ ESP32) → thêm vào waveform
        const next = r.samples?.length
          ? [...s.waveform, ...r.samples].slice(-WAVEFORM_DISPLAY)
          : s.waveform;
        return {
          ...s,
          lastResult:   r,
          deviceOnline: true,
          waveform:     next,
          bpm: bpm && bpm > 20 && bpm < 250 ? bpm : s.bpm,
        };
      });
    }

    if (topic === 'ecg/raw') {
      const r = data as EcgRaw;
      lastDeviceMsgTs.current = Date.now();
      setState(s => {
        const next = [...s.waveform, ...r.samples].slice(-WAVEFORM_DISPLAY);
        return { ...s, waveform: next, deviceOnline: true };
      });
    }

    if (topic === 'ecg/alert') {
      setState(s => ({ ...s, lastAlert: data as EcgAlert }));
    }
  }

  // Heartbeat watchdog: đánh dấu device offline nếu im lặng > 10s
  useEffect(() => {
    const id = setInterval(() => {
      const silent = Date.now() - lastDeviceMsgTs.current;
      if (lastDeviceMsgTs.current > 0 && silent > DEVICE_TIMEOUT_MS) {
        setState(s => s.deviceOnline ? { ...s, deviceOnline: false } : s);
      }
    }, 2000);
    return () => clearInterval(id);
  }, []);

  useEffect(() => {
    connect();
    return () => { clientRef.current?.end(true); };
  }, [connect]);

  return state;
}

// ── AWS SigV4 WebSocket URL builder ──────────────────────────────────────────
function buildWssUrl(host: string, region: string, ak: string, sk: string, st: string): string {
  const now     = new Date();
  const date    = formatDate(now);
  const time    = formatDateTime(now);
  const service = 'iotdevicegateway';
  const algo    = 'AWS4-HMAC-SHA256';

  const credScope  = `${date}/${region}/${service}/aws4_request`;
  const signedHdrs = 'host';

  const canonicalQS = [
    `X-Amz-Algorithm=${algo}`,
    `X-Amz-Credential=${encodeURIComponent(`${ak}/${credScope}`)}`,
    `X-Amz-Date=${time}`,
    `X-Amz-Expires=86400`,
    `X-Amz-Security-Token=${encodeURIComponent(st)}`,
    `X-Amz-SignedHeaders=${signedHdrs}`,
  ].join('&');

  const canonicalReq = [
    'GET', '/mqtt', canonicalQS,
    `host:${host}\n`, signedHdrs, sha256(''),
  ].join('\n');

  const strToSign = [algo, time, credScope, sha256(canonicalReq)].join('\n');

  const sigKey = getSignatureKey(sk, date, region, service);
  const sig    = hmacHex(sigKey, strToSign);

  return `wss://${host}/mqtt?${canonicalQS}&X-Amz-Signature=${sig}`;
}

// ── Crypto helpers (Web Crypto via subtle is async; use pure-JS for URL signing) ──
import CryptoJS from 'crypto-js';

function sha256(msg: string)             { return CryptoJS.SHA256(msg).toString(); }
function hmac(key: CryptoJS.lib.WordArray | string, msg: string) {
  return CryptoJS.HmacSHA256(msg, key);
}
function hmacHex(key: CryptoJS.lib.WordArray, msg: string) {
  return hmac(key, msg).toString();
}
function getSignatureKey(key: string, date: string, region: string, service: string) {
  return hmac(hmac(hmac(hmac(`AWS4${key}`, date), region), service), 'aws4_request');
}
function formatDate(d: Date)     { return d.toISOString().slice(0, 10).replace(/-/g, ''); }
function formatDateTime(d: Date) { return d.toISOString().slice(0, 19).replace(/[-:]/g, '') + 'Z'; }
