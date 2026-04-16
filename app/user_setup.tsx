import React, { useEffect, useState } from "react";
import { View, Text, Pressable, TextInput, Alert, ScrollView } from "react-native";
import { Stack } from "expo-router";

import type { UserDimensions } from "../src/domain/types";
import { loadUserDimensions, saveUserDimensions } from "../src/storage/registry";
import { useFontSize } from "../src/context/FontSizeContext";
import { useBle } from "../src/context/BleContext";

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
  const { fontScale } = useFontSize();
  const { sendArmLength } = useBle();

  // Metric input
  const [shoulderCmInput, setShoulderCmInput] = useState("");

  // Imperial inputs
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
        setShoulderCmInput(String(Math.round(dimensions.shoulderToFingertipCm)));
      } else {
        const s = cmToFeetInches(dimensions.shoulderToFingertipCm);
        setShoulderFtInput(String(s.feet));
        setShoulderInInput(String(s.inches));
      }
    }
    setIsEditing(true);
  }

  // Clear all inputs
  function clearInputs() {
    setShoulderCmInput("");
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
    let shoulderToFingertipCm: number;

    if (useMetric) {
      shoulderToFingertipCm = parseFloat(shoulderCmInput) || 0;
    } else {
      const sFt = parseFloat(shoulderFtInput) || 0;
      const sIn = parseFloat(shoulderInInput) || 0;
      shoulderToFingertipCm = feetInchesToCm(sFt, sIn);
    }

    if (shoulderToFingertipCm <= 0) {
      Alert.alert("Invalid Input", "Please enter a valid shoulder-to-fingertip length.");
      return;
    }

    const newDimensions: UserDimensions = { shoulderToFingertipCm };

    await saveUserDimensions(newDimensions);
    setDimensions(newDimensions);
    setIsEditing(false);

    // Push live to belt (silently no-ops if disconnected; on-connect sync will resend)
    sendArmLength(Math.round(shoulderToFingertipCm));
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
          <Text style={{ fontWeight: !useMetric ? "600" : "400", fontSize: 16 * fontScale }}>Imperial (ft/in)</Text>
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
          <Text style={{ fontWeight: useMetric ? "600" : "400", fontSize: 16 * fontScale }}>Metric (cm)</Text>
        </Pressable>
      </View>

      {/* Helper text */}
      <Text style={{ opacity: 0.6, fontSize: 14 * fontScale }}>
        {useMetric
          ? "Enter value in centimeters (cm)"
          : "Enter value in feet and inches"}
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
          <Text style={{ fontSize: 18 * fontScale, fontWeight: "600" }}>Your Dimensions</Text>

          <View style={{ gap: 4 }}>
            <Text style={{ fontSize: 16 * fontScale }}>
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
            <Text style={{ color: "black", fontWeight: "600", fontSize: 16 * fontScale }}>Edit</Text>
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
          <Text style={{ fontSize: 18 * fontScale, fontWeight: "600" }}>Enter Your Dimensions</Text>

          {/* Shoulder to Fingertip Input */}
          <View style={{ gap: 4 }}>
            <Text style={{ fontSize: 14 * fontScale, opacity: 0.7 }}>Shoulder to Fingertip</Text>
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
                  fontSize: 16 * fontScale,
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
                    fontSize: 16 * fontScale,
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
                    fontSize: 16 * fontScale,
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
                <Text style={{ fontSize: 16 * fontScale }}>Cancel</Text>
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
              <Text style={{ fontSize: 16 * fontScale }}>Clear</Text>
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
              <Text style={{ fontWeight: "600", fontSize: 16 * fontScale }}>Save</Text>
            </Pressable>
          </View>
        </View>
      )}

    </ScrollView>
  );
}
