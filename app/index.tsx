import { useCallback, useRef, useState } from "react";
import { View, Text, Pressable, Alert } from "react-native";
import { Link, useFocusEffect } from "expo-router";
import * as Speech from "expo-speech";

import type { PlacedSensor } from "../src/domain/types";
import { loadSensors } from "../src/storage/registry";
import { useFontSize } from "../src/context/FontSizeContext";

export default function HomeScreen() {
  const [sensors, setSensors] = useState<PlacedSensor[]>([]);
  const [debugBanner, setDebugBanner] = useState<string | null>(null);
  const bannerTimeoutRef = useRef<NodeJS.Timeout | null>(null);
  const { fontScale, setFontScale } = useFontSize();

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
          <Text style={{ fontWeight: "600", fontSize: 16 * fontScale }}>{debugBanner}</Text>
        </View>
      )}

      <View style={{ flex: 1, padding: 20, gap: 12 }}>

        <Link href="/user_setup" asChild>
          <Pressable style={{ padding: 12, borderWidth: 1, borderRadius: 10 }}>
            <Text style={{ fontSize: 16 * fontScale }}>Setup your Dimensions</Text>
          </Pressable>
        </Link>

        <Link href="/sensor_list" asChild>
          <Pressable style={{ padding: 12, borderWidth: 1, borderRadius: 10 }}>
            <Text style={{ fontSize: 16 * fontScale }}>Go to your Sensor List</Text>
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
