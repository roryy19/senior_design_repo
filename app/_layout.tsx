import { Stack } from "expo-router";
import { FontSizeProvider } from "../src/context/FontSizeContext";
import { BleProvider } from "../src/context/BleContext";

export default function Layout() {
  return (
    <FontSizeProvider>
      <BleProvider>
      <Stack>
        <Stack.Screen name="index" options={{ title: "Home" }} />
        <Stack.Screen name="user_setup" options={{ title: "User Setup" }} />
        <Stack.Screen name="sensor_list" options={{ title: "Sensor List" }} />
      </Stack>
      </BleProvider>
    </FontSizeProvider>
  );
}
