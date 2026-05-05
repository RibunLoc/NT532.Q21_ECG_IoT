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
  samples?:          number[];
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
  lastResult:   EcgResult | null;
  lastAlert:    EcgAlert  | null;
  waveform:     number[];
  connected:    boolean;
  deviceOnline: boolean;
  bpm:          number | null;
}

const WAVEFORM_DISPLAY  = 376;
const DEVICE_TIMEOUT_MS = 10_000;

export function useEcgStream() {
  const clientRef         = useRef<MqttClient | null>(null);
  const lastBeatTs        = useRef<number>(0);
  const lastDeviceMsgTs   = useRef<number>(0);
  const waveformRef       = useRef<number[]>([]);
  const connectingRef     = useRef(false);

  const [state, setState] = useState<EcgStreamState>({
    lastResult:   null,
    lastAlert:    null,
    waveform:     [],
    connected:    false,
    deviceOnline: false,
    bpm:          null,
  });

  const handleMessage = useCallback((topic: string, payload: Buffer) => {
    let data: unknown;
    try { data = JSON.parse(payload.toString()); } catch { return; }

    const now = Date.now();

    if (topic === 'ecg/result') {
      const r = data as EcgResult;
      const bpm = lastBeatTs.current
        ? Math.round(60000 / (now - lastBeatTs.current))
        : null;
      lastBeatTs.current      = now;
      lastDeviceMsgTs.current = now;

      if (r.samples?.length) {
        const next = [...waveformRef.current, ...r.samples].slice(-WAVEFORM_DISPLAY);
        waveformRef.current = next;
        setState(s => ({
          ...s, lastResult: r, deviceOnline: true, waveform: next,
          bpm: bpm && bpm > 20 && bpm < 250 ? bpm : s.bpm,
        }));
      } else {
        setState(s => ({
          ...s, lastResult: r, deviceOnline: true,
          bpm: bpm && bpm > 20 && bpm < 250 ? bpm : s.bpm,
        }));
      }
    }

    if (topic === 'ecg/raw') {
      const r = data as EcgRaw;
      lastDeviceMsgTs.current = now;
      const next = [...waveformRef.current, ...r.samples].slice(-WAVEFORM_DISPLAY);
      waveformRef.current = next;
      setState(s => ({ ...s, waveform: next, deviceOnline: true }));
    }

    if (topic === 'ecg/alert') {
      setState(s => ({ ...s, lastAlert: data as EcgAlert }));
    }
  }, []);

  const connect = useCallback(async () => {
    if (connectingRef.current) return;
    connectingRef.current = true;
    try {
      const session     = await fetchAuthSession();
      const credentials = session.credentials;
      if (!credentials) throw new Error('No credentials from Identity Pool');

      const { accessKeyId, secretAccessKey, sessionToken } = credentials;
      const url = await buildWssUrl(IOT_ENDPOINT, AWS_REGION, accessKeyId, secretAccessKey, sessionToken!);

      const clientId = `ecg-dashboard-${Math.random().toString(16).slice(2, 10)}`;
      const client = mqtt.connect(url, {
        protocolVersion: 4,
        protocolId:      'MQTT',
        reconnectPeriod: 0,
        keepalive:       30,
        clientId,
        clean:           true,
      });

      client.on('connect', () => {
        client.subscribe(['ecg/result', 'ecg/raw', 'ecg/alert'], { qos: 1 });
        setState(s => ({ ...s, connected: true }));
        connectingRef.current = false;
      });

      client.on('message', (topic, payload) => handleMessage(topic, payload));

      client.on('close', () => {
        setState(s => ({ ...s, connected: false }));
        connectingRef.current = false;
        setTimeout(() => connect(), 5000);
      });

      client.on('error', (err) => console.error('[MQTT]', err));

      clientRef.current = client;
    } catch (err) {
      console.error('[MQTT] connect error:', err);
      connectingRef.current = false;
      setTimeout(() => connect(), 5000);
    }
  }, [handleMessage]);

  // Watchdog: device offline nếu im lặng > 10s
  useEffect(() => {
    const id = setInterval(() => {
      if (lastDeviceMsgTs.current > 0 &&
          Date.now() - lastDeviceMsgTs.current > DEVICE_TIMEOUT_MS) {
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

// ── AWS SigV4 WebSocket URL (Web Crypto API) ─────────────────────────────────
async function buildWssUrl(
  host: string, region: string, ak: string, sk: string, st: string
): Promise<string> {
  const now       = new Date();
  const date      = now.toISOString().slice(0, 10).replace(/-/g, '');
  const dateTime  = now.toISOString().slice(0, 19).replace(/[-:]/g, '') + 'Z';
  const service   = 'iotdevicegateway';
  const algo      = 'AWS4-HMAC-SHA256';
  const credScope = `${date}/${region}/${service}/aws4_request`;

  // Canonical QS — KHÔNG có X-Amz-Security-Token (append sau khi ký)
  const signingParams: [string, string][] = [
    ['X-Amz-Algorithm',    algo],
    ['X-Amz-Credential',   `${ak}/${credScope}`],
    ['X-Amz-Date',         dateTime],
    ['X-Amz-Expires',      '86400'],
    ['X-Amz-SignedHeaders', 'host'],
  ];
  signingParams.sort(([a], [b]) => a < b ? -1 : 1);
  const canonicalQS = signingParams
    .map(([k, v]) => `${k}=${encodeURIComponent(v)}`)
    .join('&');

  const canonicalReq = ['GET', '/mqtt', canonicalQS, `host:${host}\n`, 'host', await sha256hex('')].join('\n');
  const strToSign    = [algo, dateTime, credScope, await sha256hex(canonicalReq)].join('\n');
  const sigKey       = await getSignatureKey(sk, date, region, service);
  const sig          = await hmacHex(sigKey, strToSign);

  return `wss://${host}/mqtt?${canonicalQS}&X-Amz-Security-Token=${encodeURIComponent(st)}&X-Amz-Signature=${sig}`;
}

async function sha256hex(msg: string): Promise<string> {
  const buf = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(msg));
  return Array.from(new Uint8Array(buf)).map(b => b.toString(16).padStart(2, '0')).join('');
}

async function hmacRaw(key: ArrayBuffer | string, msg: string): Promise<ArrayBuffer> {
  const keyData = typeof key === 'string' ? new TextEncoder().encode(key) : key;
  const k = await crypto.subtle.importKey('raw', keyData, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  return crypto.subtle.sign('HMAC', k, new TextEncoder().encode(msg));
}

async function hmacHex(key: ArrayBuffer, msg: string): Promise<string> {
  const buf = await hmacRaw(key, msg);
  return Array.from(new Uint8Array(buf)).map(b => b.toString(16).padStart(2, '0')).join('');
}

async function getSignatureKey(sk: string, date: string, region: string, service: string): Promise<ArrayBuffer> {
  const k1 = await hmacRaw(`AWS4${sk}`, date);
  const k2 = await hmacRaw(k1, region);
  const k3 = await hmacRaw(k2, service);
  return hmacRaw(k3, 'aws4_request');
}
