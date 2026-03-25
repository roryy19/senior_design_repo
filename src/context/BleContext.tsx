import React, { createContext, useContext, useEffect, useRef, useState } from 'react';
import { AppState } from 'react-native';
import * as Notifications from 'expo-notifications';
import { bleService, AlertPayload, BleConnectionState } from '../ble/BleService';
import { loadSensors, loadUserDimensions } from '../storage/registry';

// An alert enriched with a human-readable name (looked up from sensor list or direction map).
export type BleAlert = {
  payload: AlertPayload;
  sensorName?: string;
  id: number; // increments each time so the same alert firing twice still triggers useEffect
};

type BleContextType = {
  connectionState: BleConnectionState;
  startScan: () => void;
  disconnect: () => void;
  sendArmLength: (cm: number) => void;
  sendAudioToBeacon: (mac: string, audioBase64: string) => Promise<void>;
  registerBeacon: (mac: string) => Promise<void>;
  lastAlert: BleAlert | null;
};

const BleContext = createContext<BleContextType | null>(null);

const DIRECTION_LABELS = [
  'front', 'front-right', 'right', 'rear-right',
  'rear', 'rear-left', 'left', 'front-left',
];

// When the app is active (foreground), suppress the notification popup and sound —
// the banner + Speech.speak() in index.tsx handle it.
// When backgrounded, show the popup and play the sound.
Notifications.setNotificationHandler({
  handleNotification: async () => {
    const backgrounded = AppState.currentState !== 'active';
    return {
      shouldShowAlert: backgrounded,
      shouldPlaySound: backgrounded,
      shouldSetBadge: false,
      shouldShowBanner: backgrounded,
      shouldShowList: backgrounded,
    };
  },
});

export function BleProvider({ children }: { children: React.ReactNode }) {
  const [connectionState, setConnectionState] = useState<BleConnectionState>('disconnected');
  const [lastAlert, setLastAlert] = useState<BleAlert | null>(null);
  const alertIdRef = useRef(0);

  useEffect(() => {
    // Request notification permission on startup (iOS shows a system prompt once).
    Notifications.requestPermissionsAsync();

    bleService.onStateChanged((state) => {
      setConnectionState(state);
      if (state === 'connected') {
        (async () => {
          // Send arm length
          const dims = await loadUserDimensions();
          if (dims?.shoulderToFingertipCm) {
            bleService.sendArmLength(dims.shoulderToFingertipCm);
          }
          // Register all saved sensor MACs with the belt
          const sensors = await loadSensors();
          for (const s of sensors) {
            if (s.macAddress) {
              await bleService.registerBeacon(s.macAddress);
            }
          }
        })();
      }
    });

    bleService.onAlertReceived((payload) => {
      (async () => {
        let sensorName: string | undefined;

        if (payload.type === 'beacon') {
          // Look up the beacon's display name by MAC address from the sensor list.
          const sensors = await loadSensors();
          const matched = sensors.find((s) => s.macAddress?.toUpperCase() === payload.mac?.toUpperCase());
          sensorName = matched?.name;
        } else {
          // Map obstacle direction index → human-readable label.
          sensorName = DIRECTION_LABELS[payload.directionIndex] ?? 'nearby';
        }

        // Build the alert message (same wording as the banner + TTS in index.tsx).
        const message =
          payload.type === 'beacon'
            ? `${sensorName ?? 'Sensor'} ahead`
            : `Obstacle ${sensorName ?? 'detected'}`;

        // Schedule a local notification. When backgrounded this fires immediately
        // with sound. When foregrounded, setNotificationHandler suppresses it.
        Notifications.scheduleNotificationAsync({
          content: { title: message, sound: true },
          trigger: null, // null = fire immediately
        });

        console.log('Alert processed — setting lastAlert:', message);
        alertIdRef.current += 1;
        setLastAlert({ payload, sensorName, id: alertIdRef.current });
      })();
    });

    return () => {
      bleService.destroy();
    };
  }, []);

  // Send a TTS audio clip to the belt for a specific beacon MAC.
  // If not connected, the clip is silently skipped (caller should check connection state).
  async function sendAudioToBeacon(mac: string, audioBase64: string) {
    if (!bleService.isConnected()) {
      console.log('Belt not connected — skipping audio send for', mac);
      return;
    }
    await bleService.sendAudioClip(mac, audioBase64);
  }

  // Register a beacon MAC with the belt so it starts scanning for it.
  // Works without the TTS native module — just tells the belt "watch for this MAC".
  async function registerBeacon(mac: string) {
    if (!bleService.isConnected()) {
      console.log('Belt not connected — skipping beacon registration for', mac);
      return;
    }
    await bleService.registerBeacon(mac);
  }

  return (
    <BleContext.Provider value={{
      connectionState,
      startScan: () => bleService.startScan(),
      disconnect: () => bleService.disconnect(),
      sendArmLength: (cm) => bleService.sendArmLength(cm),
      sendAudioToBeacon,
      registerBeacon,
      lastAlert,
    }}>
      {children}
    </BleContext.Provider>
  );
}

export function useBle() {
  const ctx = useContext(BleContext);
  if (!ctx) throw new Error('useBle must be used inside BleProvider');
  return ctx;
}
