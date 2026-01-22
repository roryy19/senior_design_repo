import { View, Text, Pressable } from "react-native";
import { Link } from "expo-router";

export default function Home() {
  return (
    <View style={{ padding: 20, gap: 12 }}>
      <Text style={{ fontSize: 22, fontWeight: "700" }}>Home</Text>

      <Link href="/settings" asChild>
        <Pressable style={{ padding: 12, borderWidth: 1, borderRadius: 10 }}>
          <Text>Go to Settings</Text>
        </Pressable>
      </Link>
    </View>
  );
}
