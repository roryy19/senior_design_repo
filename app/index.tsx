import { useCallback, useRef, useState } from "react";
import { View, Text, Pressable, Alert } from "react-native";
import { Link, useFocusEffect } from "expo-router";
import * as Speech from "expo-speech";

import type { PlacedSensor } from "../src/domain/types";
import { loadSensors } from "../src/storage/registry";

export default function HomeScreen() {
  const [sensors, setSensors] = useState<PlacedSensor[]>([]);
  const [debugBanner, setDebugBanner] = useState<string | null>(null);
  const bannerTimeoutRef = useRef<NodeJS.Timeout | null>(null);

  // Reload sensors every time screen comes into focus
  useFocusEffect(
    useCallback(() => {
      (async () => {
        const saved = await loadSensors();
        setSensors(saved);
      })();
    }, [])
  );

  // Simulate sensor trigger
  function simulateTrigger() {
    if (sensors.length === 0) {
      Alert.alert("No Sensors", "Add sensors in the Sensor List first.");
      return;
    }

    // Clear previous timeout if exists
    if (bannerTimeoutRef.current) {
      clearTimeout(bannerTimeoutRef.current);
    }

    // Pick random sensor
    const randomIndex = Math.floor(Math.random() * sensors.length);
    const sensor = sensors[randomIndex];

    // Message to display and speak
    const message = `${sensor.name} ahead`;

    // Show banner
    setDebugBanner(message);

    // Speak the message
    Speech.speak(message);

    // Auto-dismiss after 3 seconds
    bannerTimeoutRef.current = setTimeout(() => {
      setDebugBanner(null);
    }, 3000);
  }

  return (
    <View style={{ flex: 1 }}>
      {/* Debug Banner */}
      {debugBanner && (
        <View
          style={{
            backgroundColor: "#ffce00",
            padding: 12,
            alignItems: "center",
          }}
        >
          <Text style={{ fontWeight: "600", fontSize: 16 }}>{debugBanner}</Text>
        </View>
      )}

      <View style={{ padding: 20, gap: 12 }}>
        <Text style={{ fontSize: 22, fontWeight: "700" }}>Home</Text>

        <Link href="/user_setup" asChild>
          <Pressable style={{ padding: 12, borderWidth: 1, borderRadius: 10 }}>
            <Text>Setup your Dimensions</Text>
          </Pressable>
        </Link>

        <Link href="/sensor_list" asChild>
          <Pressable style={{ padding: 12, borderWidth: 1, borderRadius: 10 }}>
            <Text>Go to your Sensor List</Text>
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
          <Text>Simulate Sensor Trigger</Text>
        </Pressable>
      </View>
    </View>
  );
}
