import { View, Text, Pressable } from "react-native";
import { Link } from "expo-router";

export default function HomeSceen() {
  return (
    <View style={{ padding: 20, gap: 12 }}>
      <Text style={{ fontSize: 22, fontWeight: "700" }}>Home</Text>

      <Link href="/sensor_list" asChild>
        <Pressable style={{ padding: 12, borderWidth: 1, borderRadius: 10 }}>
          <Text>Go to your Sensor List</Text>
        </Pressable>
      </Link>
    </View>
  );
}
