import { useCallback, useEffect, useRef, useState } from "react";
import { View, Text, Pressable, Alert } from "react-native";
import { Link, useFocusEffect } from "expo-router";
import * as Speech from "expo-speech";

import type { PlacedSensor } from "../src/domain/types";
import { loadSensors } from "../src/storage/registry";
import { useFontSize } from "../src/context/FontSizeContext";
import { useBle } from "../src/context/BleContext";
import type { BleConnectionState } from "../src/ble/BleService";

// Note: BLE alert banner + TTS is now handled in _layout.tsx (visible on all screens).
// The simulate button below still uses a local banner for testing.

function bleStatusLabel(state: BleConnectionState, scanDots: string): string {
  switch (state) {
    case 'scanning':   return `Scanning${scanDots}`;
    case 'connecting': return 'Connecting...';
    case 'connected':  return 'Connected to Belt';
    default:           return 'Not Connected';
  }
}

function bleStatusDotColor(state: BleConnectionState): string {
  if (state === 'connected') return '#34c759';
  if (state === 'scanning' || state === 'connecting') return '#ff9500';
  return '#999';
}

export default function HomeScreen() {
  const [sensors, setSensors] = useState<PlacedSensor[]>([]);
  const [debugBanner, setDebugBanner] = useState<string | null>(null);
  const bannerTimeoutRef = useRef<NodeJS.Timeout | null>(null);
  const { fontScale, setFontScale } = useFontSize();
  const { connectionState, startScan, disconnect } = useBle();
  const [dotCount, setDotCount] = useState(1);

  // Animate dots while scanning: . → .. → ... → . (cycles every 500ms)
  useEffect(() => {
    if (connectionState !== 'scanning') {
      setDotCount(1);
      return;
    }
    const interval = setInterval(() => {
      setDotCount(prev => (prev % 3) + 1);
    }, 500);
    return () => clearInterval(interval);
  }, [connectionState]);

  // Reload sensors every time screen comes into focus
  useFocusEffect(
    useCallback(() => {
      (async () => {
        const saved = await loadSensors();
        setSensors(saved);
      })();
    }, [])
  );

  function showAlert(message: string) {
    if (bannerTimeoutRef.current) clearTimeout(bannerTimeoutRef.current);
    setDebugBanner(message);
    Speech.speak(message);
    bannerTimeoutRef.current = setTimeout(() => setDebugBanner(null), 3000);
  }

  function simulateTrigger() {
    if (sensors.length === 0) {
      Alert.alert("No Beacons", "Add beacons in the Beacon List first.");
      return;
    }
    const sensor = sensors[Math.floor(Math.random() * sensors.length)];
    showAlert(`${sensor.name} ahead`);
  }

  function handleConnectPress() {
    if (connectionState === 'connected') {
      disconnect();
    } else {
      startScan();
    }
  }

  return (
    <View style={{ flex: 1 }}>
      {/* Alert Banner */}
      {debugBanner && (
        <View
          style={{
            backgroundColor: "#ffce00",
            padding: 12,
            alignItems: "center",
          }}
        >
          <Text style={{ fontWeight: "600", fontSize: 16 * fontScale }}>{debugBanner}</Text>
        </View>
      )}

      <View style={{ flex: 1, padding: 20, gap: 12 }}>

        {/* BLE Connection Status */}
        <View
          style={{
            flexDirection: 'row',
            alignItems: 'center',
            gap: 10,
            padding: 12,
            borderWidth: 1,
            borderRadius: 10,
            borderColor: '#ddd',
          }}
        >
          <View
            style={{
              width: 10,
              height: 10,
              borderRadius: 5,
              backgroundColor: bleStatusDotColor(connectionState),
            }}
          />
          <Text style={{ flex: 1, fontSize: 15 * fontScale }}>
            {bleStatusLabel(connectionState, '.'.repeat(dotCount))}
          </Text>
          {connectionState !== 'scanning' && connectionState !== 'connecting' && (
            <Pressable
              onPress={handleConnectPress}
              style={{
                paddingVertical: 6,
                paddingHorizontal: 14,
                borderRadius: 8,
                backgroundColor: connectionState === 'connected' ? '#ff3b30' : '#007aff',
              }}
            >
              <Text style={{ color: 'white', fontWeight: '600', fontSize: 14 * fontScale }}>
                {connectionState === 'connected' ? 'Disconnect' : 'Connect'}
              </Text>
            </Pressable>
          )}
        </View>

        <Link href="/user_setup" asChild>
          <Pressable style={{ padding: 12, borderWidth: 1, borderRadius: 10 }}>
            <Text style={{ fontSize: 16 * fontScale }}>Setup your Dimensions</Text>
          </Pressable>
        </Link>

        <Link href="/sensor_list" asChild>
          <Pressable style={{ padding: 12, borderWidth: 1, borderRadius: 10 }}>
            <Text style={{ fontSize: 16 * fontScale }}>Go to your Beacon List</Text>
          </Pressable>
        </Link>

        <Pressable
          onPress={simulateTrigger}
          style={{
            padding: 12,
            borderWidth: 1,
            borderRadius: 10,
            backgroundColor: "#f0f0f0",
          }}
        >
          <Text style={{ fontSize: 16 * fontScale }}>Simulate Sensor Trigger</Text>
        </Pressable>
      </View>

      {/* Font Size Selector */}
      <View style={{ padding: 20, paddingBottom: 40, borderTopWidth: 1, borderTopColor: "#ddd" }}>
        <Text style={{ fontSize: 14 * fontScale, opacity: 0.6, marginBottom: 8 }}>Text Size</Text>
        <View style={{ flexDirection: "row", gap: 12 }}>
          <Pressable
            onPress={() => setFontScale(1)}
            style={{
              flex: 1,
              padding: 12,
              borderRadius: 10,
              borderWidth: 1,
              borderColor: fontScale === 1 ? "#000" : "#ddd",
              backgroundColor: fontScale === 1 ? "#f0f0f0" : "white",
              alignItems: "center",
            }}
          >
            <Text style={{ fontWeight: fontScale === 1 ? "600" : "400", fontSize: 14 * fontScale }}>Default</Text>
          </Pressable>

          <Pressable
            onPress={() => setFontScale(1.25)}
            style={{
              flex: 1,
              padding: 12,
              borderRadius: 10,
              borderWidth: 1,
              borderColor: fontScale === 1.25 ? "#000" : "#ddd",
              backgroundColor: fontScale === 1.25 ? "#f0f0f0" : "white",
              alignItems: "center",
            }}
          >
            <Text style={{ fontWeight: fontScale === 1.25 ? "600" : "400", fontSize: 14 * fontScale }}>Large</Text>
          </Pressable>

          <Pressable
            onPress={() => setFontScale(1.5)}
            style={{
              flex: 1,
              padding: 12,
              borderRadius: 10,
              borderWidth: 1,
              borderColor: fontScale === 1.5 ? "#000" : "#ddd",
              backgroundColor: fontScale === 1.5 ? "#f0f0f0" : "white",
              alignItems: "center",
            }}
          >
            <Text style={{ fontWeight: fontScale === 1.5 ? "600" : "400", fontSize: 14 * fontScale }}>Max</Text>
          </Pressable>
        </View>
      </View>
    </View>
  );
}
