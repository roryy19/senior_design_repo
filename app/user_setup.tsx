import React, { useEffect, useState } from "react";
import { View, Text, Pressable, TextInput, Alert, ScrollView } from "react-native";
import { Stack } from "expo-router";

import type { UserDimensions } from "../src/domain/types";
import { loadUserDimensions, saveUserDimensions } from "../src/storage/registry";

// Conversion helpers
function cmToFeetInches(cm: number): { feet: number; inches: number } {
  const totalInches = cm / 2.54;
  const feet = Math.floor(totalInches / 12);
  const inches = Math.round(totalInches % 12);
  return { feet, inches };
}

function feetInchesToCm(feet: number, inches: number): number {
  return (feet * 12 + inches) * 2.54;
}

export default function UserSetupScreen() {
  // Saved dimensions from storage
  const [dimensions, setDimensions] = useState<UserDimensions | null>(null);
  const [isEditing, setIsEditing] = useState(false);
  const [useMetric, setUseMetric] = useState(false);

  // Metric inputs
  const [heightCmInput, setHeightCmInput] = useState("");
  const [beltCmInput, setBeltCmInput] = useState("");
  const [shoulderCmInput, setShoulderCmInput] = useState("");

  // Imperial inputs
  const [heightFtInput, setHeightFtInput] = useState("");
  const [heightInInput, setHeightInInput] = useState("");
  const [beltFtInput, setBeltFtInput] = useState("");
  const [beltInInput, setBeltInInput] = useState("");
  const [shoulderFtInput, setShoulderFtInput] = useState("");
  const [shoulderInInput, setShoulderInInput] = useState("");

  // Load saved dimensions on mount
  useEffect(() => {
    (async () => {
      const saved = await loadUserDimensions();
      setDimensions(saved);
      if (!saved) {
        setIsEditing(true); // No dimensions yet, start in edit mode
      }
    })();
  }, []);

  // Populate inputs when entering edit mode
  function startEdit() {
    if (dimensions) {
      if (useMetric) {
        setHeightCmInput(String(Math.round(dimensions.heightCm)));
        setBeltCmInput(String(Math.round(dimensions.groundToBeltCm)));
        setShoulderCmInput(String(Math.round(dimensions.shoulderToFingertipCm)));
      } else {
        const h = cmToFeetInches(dimensions.heightCm);
        const b = cmToFeetInches(dimensions.groundToBeltCm);
        const s = cmToFeetInches(dimensions.shoulderToFingertipCm);
        setHeightFtInput(String(h.feet));
        setHeightInInput(String(h.inches));
        setBeltFtInput(String(b.feet));
        setBeltInInput(String(b.inches));
        setShoulderFtInput(String(s.feet));
        setShoulderInInput(String(s.inches));
      }
    }
    setIsEditing(true);
  }

  // Clear all inputs
  function clearInputs() {
    setHeightCmInput("");
    setBeltCmInput("");
    setShoulderCmInput("");
    setHeightFtInput("");
    setHeightInInput("");
    setBeltFtInput("");
    setBeltInInput("");
    setShoulderFtInput("");
    setShoulderInInput("");
  }

  // Cancel editing
  function cancelEdit() {
    if (dimensions) {
      setIsEditing(false);
    }
    clearInputs();
  }

  // Save dimensions
  async function saveDimensions() {
    let heightCm: number;
    let groundToBeltCm: number;
    let shoulderToFingertipCm: number;

    if (useMetric) {
      heightCm = parseFloat(heightCmInput) || 0;
      groundToBeltCm = parseFloat(beltCmInput) || 0;
      shoulderToFingertipCm = parseFloat(shoulderCmInput) || 0;
    } else {
      const hFt = parseFloat(heightFtInput) || 0;
      const hIn = parseFloat(heightInInput) || 0;
      const bFt = parseFloat(beltFtInput) || 0;
      const bIn = parseFloat(beltInInput) || 0;
      const sFt = parseFloat(shoulderFtInput) || 0;
      const sIn = parseFloat(shoulderInInput) || 0;
      heightCm = feetInchesToCm(hFt, hIn);
      groundToBeltCm = feetInchesToCm(bFt, bIn);
      shoulderToFingertipCm = feetInchesToCm(sFt, sIn);
    }

    if (heightCm <= 0 || groundToBeltCm <= 0 || shoulderToFingertipCm <= 0) {
      Alert.alert("Invalid Input", "Please enter valid values for all fields.");
      return;
    }

    if (groundToBeltCm >= heightCm) {
      Alert.alert("Invalid Input", "Ground to belt must be less than total height.");
      return;
    }

    const newDimensions: UserDimensions = {
      heightCm,
      groundToBeltCm,
      beltToHeadCm: heightCm - groundToBeltCm,
      shoulderToFingertipCm,
      frontSensorDistanceAtTouch: dimensions?.frontSensorDistanceAtTouch,
    };

    await saveUserDimensions(newDimensions);
    setDimensions(newDimensions);
    setIsEditing(false);
  }

  // Configure button (placeholder)
  function handleConfigure() {
    Alert.alert(
      "Configure Front Sensor",
      "This feature requires the belt sensor to be connected. Stand with your arm extended and fingertips touching a wall to calibrate.",
      [{ text: "OK" }]
    );
  }

  // Format display value based on unit
  function formatValue(cm: number): string {
    if (useMetric) {
      return `${Math.round(cm)} cm`;
    } else {
      const { feet, inches } = cmToFeetInches(cm);
      return `${feet}' ${inches}"`;
    }
  }

  return (
    <ScrollView style={{ flex: 1 }} contentContainerStyle={{ padding: 16, gap: 16 }}>
      <Stack.Screen options={{ title: "User Setup" }} />

      {/* Unit Toggle */}
      <View style={{ flexDirection: "row", gap: 12 }}>
        <Pressable
          onPress={() => setUseMetric(false)}
          style={{
            flex: 1,
            padding: 12,
            borderRadius: 10,
            borderWidth: 1,
            borderColor: !useMetric ? "#000" : "#ddd",
            backgroundColor: !useMetric ? "#f0f0f0" : "white",
            alignItems: "center",
          }}
        >
          <Text style={{ fontWeight: !useMetric ? "600" : "400" }}>Imperial (ft/in)</Text>
        </Pressable>

        <Pressable
          onPress={() => setUseMetric(true)}
          style={{
            flex: 1,
            padding: 12,
            borderRadius: 10,
            borderWidth: 1,
            borderColor: useMetric ? "#000" : "#ddd",
            backgroundColor: useMetric ? "#f0f0f0" : "white",
            alignItems: "center",
          }}
        >
          <Text style={{ fontWeight: useMetric ? "600" : "400" }}>Metric (cm)</Text>
        </Pressable>
      </View>

      {/* Helper text */}
      <Text style={{ opacity: 0.6, fontSize: 14 }}>
        {useMetric
          ? "Enter values in centimeters (cm)"
          : "Enter values in feet and inches"}
      </Text>

      {/* Dimension Card (locked state) or Inputs (editing state) */}
      {!isEditing && dimensions ? (
        <View
          style={{
            borderWidth: 1,
            borderColor: "#ddd",
            borderRadius: 12,
            padding: 12,
            gap: 8,
          }}
        >
          <Text style={{ fontSize: 18, fontWeight: "600" }}>Your Dimensions</Text>

          <View style={{ gap: 4 }}>
            <Text style={{ fontSize: 16 }}>
              Height: {formatValue(dimensions.heightCm)}
            </Text>
            <Text style={{ fontSize: 16 }}>
              Ground to Belt: {formatValue(dimensions.groundToBeltCm)}
            </Text>
            <Text style={{ fontSize: 16, opacity: 0.7 }}>
              Belt to Head: {formatValue(dimensions.beltToHeadCm)}
            </Text>
            <Text style={{ fontSize: 16 }}>
              Shoulder to Fingertip: {formatValue(dimensions.shoulderToFingertipCm)}
            </Text>
          </View>

          <Pressable
            onPress={startEdit}
            style={{
              marginTop: 8,
              paddingVertical: 8,
              paddingHorizontal: 12,
              borderRadius: 10,
              backgroundColor: "#ffce00",
              alignSelf: "flex-start",
            }}
          >
            <Text style={{ color: "black", fontWeight: "600" }}>Edit</Text>
          </Pressable>
        </View>
      ) : (
        <View
          style={{
            borderWidth: 1,
            borderColor: "#ddd",
            borderRadius: 12,
            padding: 12,
            gap: 12,
          }}
        >
          <Text style={{ fontSize: 18, fontWeight: "600" }}>Enter Your Dimensions</Text>

          {/* Height Input */}
          <View style={{ gap: 4 }}>
            <Text style={{ fontSize: 14, opacity: 0.7 }}>Height</Text>
            {useMetric ? (
              <TextInput
                value={heightCmInput}
                onChangeText={setHeightCmInput}
                placeholder="Height in cm (e.g., 175)"
                placeholderTextColor="#999"
                keyboardType="numeric"
                style={{
                  borderWidth: 1,
                  borderColor: "#ddd",
                  borderRadius: 10,
                  padding: 12,
                  fontSize: 16,
                }}
              />
            ) : (
              <View style={{ flexDirection: "row", gap: 8 }}>
                <TextInput
                  value={heightFtInput}
                  onChangeText={setHeightFtInput}
                  placeholder="Feet (e.g., 5)"
                  placeholderTextColor="#999"
                  keyboardType="numeric"
                  style={{
                    flex: 1,
                    borderWidth: 1,
                    borderColor: "#ddd",
                    borderRadius: 10,
                    padding: 12,
                    fontSize: 16,
                  }}
                />
                <TextInput
                  value={heightInInput}
                  onChangeText={setHeightInInput}
                  placeholder="Inches (e.g., 9)"
                  placeholderTextColor="#999"
                  keyboardType="numeric"
                  style={{
                    flex: 1,
                    borderWidth: 1,
                    borderColor: "#ddd",
                    borderRadius: 10,
                    padding: 12,
                    fontSize: 16,
                  }}
                />
              </View>
            )}
          </View>

          {/* Ground to Belt Input */}
          <View style={{ gap: 4 }}>
            <Text style={{ fontSize: 14, opacity: 0.7 }}>Ground to Belt</Text>
            {useMetric ? (
              <TextInput
                value={beltCmInput}
                onChangeText={setBeltCmInput}
                placeholder="Ground to belt in cm (e.g., 100)"
                placeholderTextColor="#999"
                keyboardType="numeric"
                style={{
                  borderWidth: 1,
                  borderColor: "#ddd",
                  borderRadius: 10,
                  padding: 12,
                  fontSize: 16,
                }}
              />
            ) : (
              <View style={{ flexDirection: "row", gap: 8 }}>
                <TextInput
                  value={beltFtInput}
                  onChangeText={setBeltFtInput}
                  placeholder="Feet (e.g., 3)"
                  placeholderTextColor="#999"
                  keyboardType="numeric"
                  style={{
                    flex: 1,
                    borderWidth: 1,
                    borderColor: "#ddd",
                    borderRadius: 10,
                    padding: 12,
                    fontSize: 16,
                  }}
                />
                <TextInput
                  value={beltInInput}
                  onChangeText={setBeltInInput}
                  placeholder="Inches (e.g., 3)"
                  placeholderTextColor="#999"
                  keyboardType="numeric"
                  style={{
                    flex: 1,
                    borderWidth: 1,
                    borderColor: "#ddd",
                    borderRadius: 10,
                    padding: 12,
                    fontSize: 16,
                  }}
                />
              </View>
            )}
          </View>

          {/* Shoulder to Fingertip Input */}
          <View style={{ gap: 4 }}>
            <Text style={{ fontSize: 14, opacity: 0.7 }}>Shoulder to Fingertip</Text>
            {useMetric ? (
              <TextInput
                value={shoulderCmInput}
                onChangeText={setShoulderCmInput}
                placeholder="Shoulder to fingertip in cm (e.g., 65)"
                placeholderTextColor="#999"
                keyboardType="numeric"
                style={{
                  borderWidth: 1,
                  borderColor: "#ddd",
                  borderRadius: 10,
                  padding: 12,
                  fontSize: 16,
                }}
              />
            ) : (
              <View style={{ flexDirection: "row", gap: 8 }}>
                <TextInput
                  value={shoulderFtInput}
                  onChangeText={setShoulderFtInput}
                  placeholder="Feet (e.g., 2)"
                  placeholderTextColor="#999"
                  keyboardType="numeric"
                  style={{
                    flex: 1,
                    borderWidth: 1,
                    borderColor: "#ddd",
                    borderRadius: 10,
                    padding: 12,
                    fontSize: 16,
                  }}
                />
                <TextInput
                  value={shoulderInInput}
                  onChangeText={setShoulderInInput}
                  placeholder="Inches (e.g., 2)"
                  placeholderTextColor="#999"
                  keyboardType="numeric"
                  style={{
                    flex: 1,
                    borderWidth: 1,
                    borderColor: "#ddd",
                    borderRadius: 10,
                    padding: 12,
                    fontSize: 16,
                  }}
                />
              </View>
            )}
          </View>

          {/* Save/Cancel/Clear buttons */}
          <View style={{ flexDirection: "row", gap: 12, marginTop: 4 }}>
            {dimensions && (
              <Pressable
                onPress={cancelEdit}
                style={{
                  flex: 1,
                  padding: 12,
                  borderRadius: 10,
                  borderWidth: 1,
                  borderColor: "#ddd",
                  alignItems: "center",
                }}
              >
                <Text>Cancel</Text>
              </Pressable>
            )}

            <Pressable
              onPress={clearInputs}
              style={{
                flex: 1,
                padding: 12,
                borderRadius: 10,
                borderWidth: 1,
                borderColor: "#ddd",
                alignItems: "center",
              }}
            >
              <Text>Clear</Text>
            </Pressable>

            <Pressable
              onPress={saveDimensions}
              style={{
                flex: 1,
                padding: 12,
                borderRadius: 10,
                borderWidth: 1,
                borderColor: "#ddd",
                alignItems: "center",
              }}
            >
              <Text style={{ fontWeight: "600" }}>Save</Text>
            </Pressable>
          </View>
        </View>
      )}

      {/* Configure Button */}
      <View style={{ marginTop: 16 }}>
        <Pressable
          onPress={handleConfigure}
          style={{
            padding: 14,
            borderRadius: 10,
            borderWidth: 1,
            borderColor: "#ccc",
            backgroundColor: "#f5f5f5",
            alignItems: "center",
          }}
        >
          <Text style={{ fontWeight: "600", color: "#666" }}>Configure Front Sensor</Text>
        </Pressable>
        <Text style={{ marginTop: 8, fontSize: 12, opacity: 0.5, textAlign: "center" }}>
          Requires belt sensor connection
        </Text>
      </View>
    </ScrollView>
  );
}
