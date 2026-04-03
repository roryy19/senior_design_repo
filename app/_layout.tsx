import { useEffect, useRef, useState } from "react";
import { View, Text } from "react-native";
import { Stack } from "expo-router";
import * as Speech from "expo-speech";
import { FontSizeProvider } from "../src/context/FontSizeContext";
import { BleProvider, useBle } from "../src/context/BleContext";
import { useFontSize } from "../src/context/FontSizeContext";

function AlertBanner() {
  const { lastAlert } = useBle();
  const { fontScale } = useFontSize();
  const [banner, setBanner] = useState<string | null>(null);
  const timeoutRef = useRef<NodeJS.Timeout | null>(null);

  useEffect(() => {
    if (!lastAlert) return;
    const message =
      lastAlert.payload.type === 'beacon'
        ? `${lastAlert.sensorName ?? 'Sensor'} ahead`
        : `Obstacle ${lastAlert.sensorName ?? 'detected'}`;

    if (timeoutRef.current) clearTimeout(timeoutRef.current);
    setBanner(message);
    Speech.speak(message);
    timeoutRef.current = setTimeout(() => setBanner(null), 3000);
  }, [lastAlert?.id]);

  if (!banner) return null;

  return (
    <View style={{
      position: 'absolute',
      bottom: 50,
      left: 16,
      right: 16,
      backgroundColor: "#ffce00",
      padding: 16,
      borderRadius: 12,
      alignItems: "center",
      zIndex: 100,
      shadowColor: '#000',
      shadowOffset: { width: 0, height: 2 },
      shadowOpacity: 0.25,
      shadowRadius: 4,
    }}>
      <Text style={{ fontWeight: "700", fontSize: 18 * fontScale }}>{banner}</Text>
    </View>
  );
}

export default function Layout() {
  return (
    <FontSizeProvider>
      <BleProvider>
        <View style={{ flex: 1 }}>
          <Stack>
            <Stack.Screen name="index" options={{ title: "Home" }} />
            <Stack.Screen name="user_setup" options={{ title: "User Setup" }} />
            <Stack.Screen name="sensor_list" options={{ title: "Beacons" }} />
          </Stack>
          <AlertBanner />
        </View>
      </BleProvider>
    </FontSizeProvider>
  );
}
