import { Stack } from "expo-router";
import { FontSizeProvider } from "../src/context/FontSizeContext";

export default function Layout() {
  return (
    <FontSizeProvider>
      <Stack>
        <Stack.Screen name="index" options={{ title: "Home" }} />
        <Stack.Screen name="user_setup" options={{ title: "User Setup" }} />
        <Stack.Screen name="sensor_list" options={{ title: "Sensor List" }} />
      </Stack>
    </FontSizeProvider>
  );
}
