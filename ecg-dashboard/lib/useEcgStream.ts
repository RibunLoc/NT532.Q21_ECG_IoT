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
  samples?:          number[];   // downsampled beat, added by ESP32 fix
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

const WAVEFORM_DISPLAY  = 376;    // ~8 beats × 47 samples
const DEVICE_TIMEOUT_MS = 10_000;

export function useEcgStream() {
  const clientRef       = useRef<MqttClient | null>(null);
  const lastBeatTs      = useRef<number>(0);
  const lastDeviceMsgTs = useRef<number>(0);

  // Dùng ref cho waveform để tránh stale closure trong message handler
  const waveformRef = useRef<number[]>([]);

  const [state, setState] = useState<EcgStreamState>({
    lastResult:   null,
    lastAlert:    null,
    waveform:     [],
    connected:    false,
    deviceOnline: false,
    bpm:          null,
  });

  // handleMessage dùng ref → không bao giờ stale
  const handleMessage = useCallback((topic: string, payload: Buffer) => {
    let data: unknown;
    try {
      data = JSON.parse(payload.toString());
    } catch {
      return;
    }

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
          ...s,
          lastResult:   r,
          deviceOnline: true,
          waveform:     next,
          bpm: bpm && bpm > 20 && bpm < 250 ? bpm : s.bpm,
        }));
      } else {
        setState(s => ({
          ...s,
          lastResult:   r,
          deviceOnline: true,
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
    try {
      const session     = await fetchAuthSession();
      const credentials = session.credentials;
      if (!credentials) throw new Error('No credentials');

      const { accessKeyId, secretAccessKey, sessionToken } = credentials;
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

      // Truyền payload thô vào handler — không parse ở đây để tránh stale closure
      client.on('message', (topic, payload) => handleMessage(topic, payload));

      client.on('close', () => setState(s => ({ ...s, connected: false })));
      client.on('error',  (err) => console.error('[MQTT]', err));

      clientRef.current = client;
    } catch (err) {
      console.error('[useEcgStream] connect error:', err);
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

import CryptoJS from 'crypto-js';

function sha256(msg: string) { return CryptoJS.SHA256(msg).toString(); }
function hmac(key: CryptoJS.lib.WordArray | string, msg: string) {
  return CryptoJS.HmacSHA256(msg, key);
}
function hmacHex(key: CryptoJS.lib.WordArray, msg: string) { return hmac(key, msg).toString(); }
function getSignatureKey(key: string, date: string, region: string, service: string) {
  return hmac(hmac(hmac(hmac(`AWS4${key}`, date), region), service), 'aws4_request');
}
function formatDate(d: Date)     { return d.toISOString().slice(0, 10).replace(/-/g, ''); }
function formatDateTime(d: Date) { return d.toISOString().slice(0, 19).replace(/[-:]/g, '') + 'Z'; }
