import React, { useEffect, useRef, useState } from "react";
import { View, Text, FlatList, Pressable, Modal, TextInput, Alert } from "react-native";
import { Stack } from "expo-router";

import type { PlacedSensor } from "../src/domain/types";
import {
  loadSensors,
  saveSensors,
  updateSensor,
  removeSensor,
  loadRssiThreshold,
  saveRssiThreshold,
  DEFAULT_RSSI_THRESHOLD,
} from "../src/storage/registry";
import { useFontSize } from "../src/context/FontSizeContext";
import { useBle } from "../src/context/BleContext";
import { generateSpeechAudio } from "../src/native/TtsSynthesizer";

// ID generator for sensors
function makeId(): string {
  return Math.random().toString(36).slice(2, 10);
}

export default function SensorListScreen() {
  // React state: the list shown in the UI
  // sensors: array of sensors
  // setSensors: function to replace sensors with this new value then re-render screen
  const [sensors, setSensors] = useState<PlacedSensor[]>([]);
  const { fontScale } = useFontSize();
  const { sendAudioToBeacon, registerBeacon, deleteBeacon, sendRssiThreshold, lastAlert, beaconRssi } = useBle();
  const [isSending, setIsSending] = useState(false);
  const [threshold, setThreshold] = useState(String(DEFAULT_RSSI_THRESHOLD));
  const [savedThreshold, setSavedThreshold] = useState(DEFAULT_RSSI_THRESHOLD);
  const leaveThreshold = (parseInt(threshold) || DEFAULT_RSSI_THRESHOLD) - 5;

  function applyThreshold() {
    let val = parseInt(threshold);
    if (isNaN(val)) val = DEFAULT_RSSI_THRESHOLD;
    if (val > -55) val = -55;
    if (val < -85) val = -85;
    setThreshold(String(val));
    setSavedThreshold(val);
    saveRssiThreshold(val);
    sendRssiThreshold(val);
  }
  const pendingMacRef = useRef<string | null>(null);
  const detectionTimerRef = useRef<NodeJS.Timeout | null>(null);

  // UI state for "Add Sensor" modal
  const [addOpen, setAddOpen] = useState(false);
  const [newName, setNewName] = useState("");
  const [newMac, setNewMac] = useState("");

  // UI state for "Edit Sensor" modal
  const [editOpen, setEditOpen] = useState(false);
  const [editingSensor, setEditingSensor] = useState<PlacedSensor | null>(null);

  // Load saved sensors when the screen first mounts
  useEffect(() => {
    (async () => {
      const saved = await loadSensors();
      setSensors(saved);
    })();
  }, []);

  // Load persisted RSSI threshold so it survives screen unmount / app restart
  useEffect(() => {
    (async () => {
      const saved = await loadRssiThreshold();
      setThreshold(String(saved));
      setSavedThreshold(saved);
    })();
  }, []);

  // If we're waiting for a beacon detection, cancel the timer if the alert matches
  useEffect(() => {
    if (!lastAlert || !pendingMacRef.current) return;
    if (lastAlert.payload.type === 'beacon' &&
        lastAlert.payload.mac?.toUpperCase() === pendingMacRef.current.toUpperCase()) {
      // Beacon was detected — clear the pending timer
      if (detectionTimerRef.current) clearTimeout(detectionTimerRef.current);
      pendingMacRef.current = null;
    }
  }, [lastAlert?.id]);

  // Open add modal
  function openAdd() {
    setNewName("");
    setNewMac("");
    setAddOpen(true);
  }

  // Generate TTS audio and send it to the belt for a sensor with a MAC address.
  async function sendAudioForSensor(name: string, mac: string | undefined) {
    if (!mac) return;
    setIsSending(true);
    try {
      const ttsText = `${name} ahead`;
      console.log('[TTS] Generating audio for:', ttsText);
      const audioBase64 = await generateSpeechAudio(ttsText);
      console.log('[TTS] Audio result:', audioBase64 ? `${audioBase64.length} chars` : 'null');
      if (audioBase64) {
        await sendAudioToBeacon(mac, audioBase64);
      }
    } catch (e) {
      console.warn('Failed to send audio to belt:', e);
    } finally {
      setIsSending(false);
    }
  }

  // Add a sensor (update state + persist + send audio to belt)
  async function confirmAdd() {
    const name = newName.trim();
    if (!name) return;

    const mac = newMac.trim() || undefined;
    const newSensor: PlacedSensor = {
      id: makeId(),
      name,
      macAddress: mac,
    };

    const next = [newSensor, ...sensors];
    setSensors(next);
    await saveSensors(next);
    setAddOpen(false);

    // Register beacon MAC with belt and start detection timer
    if (mac) {
      registerBeacon(mac);
      startDetectionTimer(mac);
    }

    // Generate TTS and send audio clip to the belt (runs in background)
    sendAudioForSensor(name, mac);
  }

  // Start a 15-second timer to warn if a beacon MAC is not detected
  function startDetectionTimer(mac: string) {
    if (detectionTimerRef.current) clearTimeout(detectionTimerRef.current);
    pendingMacRef.current = mac;
    detectionTimerRef.current = setTimeout(() => {
      if (pendingMacRef.current === mac) {
        Alert.alert(
          "Beacon Not Found",
          "This beacon was not detected nearby. Check that the MAC address is correct and the beacon is powered on."
        );
        pendingMacRef.current = null;
      }
    }, 15000);
  }

  // Open edit modal
  function openEdit(sensor: PlacedSensor) {
    setEditingSensor(sensor); // sensor that is being edited
    setNewName(sensor.name); // prefill input with current name
    setNewMac(sensor.macAddress ?? "");
    setEditOpen(true);
  }

  // Edit name of existing sensor
  async function confirmEdit() {
    const name = newName.trim();
    if (!name || !editingSensor) return;

    const mac = newMac.trim() || undefined;
    const updated: PlacedSensor = { ...editingSensor, name, macAddress: mac };

    const next = await updateSensor(updated);
    setSensors(next);

    setEditOpen(false);
    setEditingSensor(null);

    // Register beacon MAC with belt and start detection timer
    if (mac) {
      registerBeacon(mac);
      startDetectionTimer(mac);
    }

    // Re-generate TTS and send updated audio clip to the belt
    sendAudioForSensor(name, mac);
  }

  function confirmDelete(sensor: PlacedSensor) {
    Alert.alert(
      "Delete beacon?",
      `Are you sure you want to delete "${sensor.name}"?`,
      [
        {text : "Cancel", style: "cancel"},
        {
          text: "Delete",
          style: "destructive",
          onPress: async () => {
            if (sensor.macAddress) {
              deleteBeacon(sensor.macAddress);
            }
            const next = await removeSensor(sensor.id);
            setSensors(next);
          },
        },
      ]
    );
  }

  // Render 1 row in the list
  function renderItem({ item }: { item: PlacedSensor }) {
    return (
      <View
        style={{
          borderWidth: 1,
          borderColor: "#ddd",
          borderRadius: 12,
          padding: 12,
        }}
      >
        <Text style={{ fontSize: 18 * fontScale, fontWeight: "600" }}>{item.name}</Text>
        {item.macAddress && (
          <Text style={{ fontSize: 13 * fontScale, color: '#000', marginTop: 2 }}>{item.macAddress}</Text>
        )}
        {item.macAddress && (
          <Text style={{
            fontSize: 13 * fontScale,
            color: beaconRssi[item.macAddress.toUpperCase()] !== undefined ? '#007AFF' : '#999',
            marginTop: 2,
          }}>
            {beaconRssi[item.macAddress.toUpperCase()] !== undefined
              ? `Signal: ${beaconRssi[item.macAddress.toUpperCase()]} dBm`
              : 'No signal'}
          </Text>
        )}

        <View
          style={{
            marginTop: 10,
            flexDirection: "row",
            justifyContent: "space-between",
            alignItems: "center",
          }}
        >
          <Pressable
            onPress={() => openEdit(item)}
            style={{
              paddingVertical: 8,
              paddingHorizontal: 12,
              borderRadius: 10,
              backgroundColor: "#ffce00",
            }}
          >
            <Text style={{ color: "black", fontWeight: "600", fontSize: 16 * fontScale }}>Edit</Text>
          </Pressable>

          <Pressable
            onPress={() => confirmDelete(item)}
            style={{
              paddingVertical: 8,
              paddingHorizontal: 12,
              borderRadius: 10,
              backgroundColor: "#ff3b30",
            }}
          >
            <Text style={{ color: "white", fontWeight: "600", fontSize: 16 * fontScale }}>Delete</Text>
          </Pressable>
        </View>
      </View>
    );
  }

  return (
    <View style={{ flex: 1 }}>
      {/* This configures the top header for this screen */}
      <Stack.Screen
        options={{
          title: "Beacons",
          headerRight: () => (
            <Pressable onPress={openAdd} style={{ paddingHorizontal: 12, paddingVertical: 6 }}>
              <Text style={{ fontSize: 16 * fontScale }}>Add</Text>
            </Pressable>
          ),
        }}
      />

      {/* Detection threshold control */}
      <View style={{ paddingHorizontal: 16, paddingTop: 12, paddingBottom: 4 }}>
        <Text style={{ fontSize: 16 * fontScale, fontWeight: "600", marginBottom: 6 }}>
          Detection Threshold
        </Text>
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
          <TextInput
            value={threshold}
            onChangeText={setThreshold}
            keyboardType="numbers-and-punctuation"
            style={{
              borderWidth: 1,
              borderColor: parseInt(threshold) !== savedThreshold ? '#007AFF' : '#ddd',
              borderRadius: 10,
              padding: 10,
              fontSize: 16 * fontScale,
              width: 80,
              textAlign: 'center',
            }}
          />
          <Text style={{ fontSize: 14 * fontScale, color: '#333' }}>dBm</Text>
          <Pressable
            onPress={applyThreshold}
            style={{
              paddingVertical: 8,
              paddingHorizontal: 14,
              borderRadius: 10,
              backgroundColor: '#007AFF',
            }}
          >
            <Text style={{ color: 'white', fontWeight: '600', fontSize: 14 * fontScale }}>Save</Text>
          </Pressable>
          <Pressable
            onPress={() => {
              setThreshold(String(DEFAULT_RSSI_THRESHOLD));
              setSavedThreshold(DEFAULT_RSSI_THRESHOLD);
              saveRssiThreshold(DEFAULT_RSSI_THRESHOLD);
              sendRssiThreshold(DEFAULT_RSSI_THRESHOLD);
            }}
            style={{
              paddingVertical: 8,
              paddingHorizontal: 14,
              borderRadius: 10,
              borderWidth: 1,
              borderColor: '#ddd',
            }}
          >
            <Text style={{ fontSize: 14 * fontScale }}>Reset</Text>
          </Pressable>
        </View>
        <Text style={{ fontSize: 12 * fontScale, color: '#000', marginTop: 4 }}>
          Range: -85 to -55 dBm
        </Text>
        <Text style={{ fontSize: 12 * fontScale, color: '#000', marginTop: 2 }}>
          Lower values detect beacons from further away.
        </Text>
        <Text style={{ fontSize: 12 * fontScale, color: '#000', marginTop: 6 }}>
          Leave threshold: {leaveThreshold} dBm
        </Text>
        <Text style={{ fontSize: 12 * fontScale, color: '#000', marginTop: 2 }}>
          Auto-set 5 below to prevent repeated alerts near the boundary.
        </Text>
      </View>

      {/* Audio transfer status banner */}
      {isSending && (
        <View style={{ backgroundColor: '#007AFF', paddingVertical: 8, paddingHorizontal: 16 }}>
          <Text style={{ color: 'white', fontSize: 14 * fontScale, textAlign: 'center' }}>
            Sending audio to belt...
          </Text>
        </View>
      )}

      {/* The vertical list */}
      <FlatList
        data={sensors}
        keyExtractor={(item) => item.id}
        contentContainerStyle={{ padding: 16 }}
        ItemSeparatorComponent={() => <View style={{ height: 10 }} />}
        ListEmptyComponent={() => (
          <Text style={{ opacity: 0.6, fontSize: 16 * fontScale }}>
            No beacons yet. Tap "Add" to create one.
          </Text>
        )}
        renderItem={renderItem}
      />

      {/* Add Sensor Modal */}
      <Modal visible={addOpen} transparent animationType="fade">
        <View
          style={{
            flex: 1,
            backgroundColor: "rgba(0,0,0,0.35)",
            justifyContent: "center",
            padding: 16,
          }}
        >
          <View
            style={{
              backgroundColor: "white",
              borderRadius: 14,
              padding: 16,
              gap: 12,
            }}
          >
            <Text style={{ fontSize: 20 * fontScale, fontWeight: "700" }}>Add beacon</Text>

            <TextInput
              value={newName}
              onChangeText={setNewName}
              placeholder="Name (e.g., Stairs)"
              placeholderTextColor="#666"
              autoFocus
              style={{
                borderWidth: 1,
                borderColor: "#ddd",
                borderRadius: 10,
                padding: 12,
                fontSize: 16 * fontScale,
              }}
            />

            <TextInput
              value={newMac}
              onChangeText={setNewMac}
              placeholder="MAC Address (e.g., AA:BB:CC:DD:EE:FF)"
              placeholderTextColor="#666"
              autoCapitalize="characters"
              style={{
                borderWidth: 1,
                borderColor: "#ddd",
                borderRadius: 10,
                padding: 12,
                fontSize: 16 * fontScale,
              }}
            />

            <View style={{ flexDirection: "row", gap: 12 }}>
              <Pressable
                onPress={() => setAddOpen(false)}
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

              <Pressable
                onPress={confirmAdd}
                style={{
                  flex: 1,
                  padding: 12,
                  borderRadius: 10,
                  borderWidth: 1,
                  borderColor: "#ddd",
                  alignItems: "center",
                }}
              >
                <Text style={{ fontWeight: "600", fontSize: 16 * fontScale }}>Add</Text>
              </Pressable>
            </View>
          </View>
        </View>
      </Modal>

      {/* Edit Existing Sensor Modal */}
      <Modal visible={editOpen} transparent animationType="fade">
        <View
          style={{
            flex: 1,
            backgroundColor: "rgba(0,0,0,0.35)",
            justifyContent: "center",
            padding: 16,
          }}
        >
          <View
            style={{
              backgroundColor: "white",
              borderRadius: 14,
              padding: 16,
              gap: 12,
            }}
          >
            <Text style={{ fontSize: 20 * fontScale, fontWeight: "700" }}>Edit beacon</Text>

            <TextInput
              value={newName}
              onChangeText={setNewName}
              placeholder="Name (e.g., Stairs)"
              autoFocus
              style={{
                borderWidth: 1,
                borderColor: "#ddd",
                borderRadius: 10,
                padding: 12,
                fontSize: 16 * fontScale,
              }}
            />

            <TextInput
              value={newMac}
              onChangeText={setNewMac}
              placeholder="MAC Address (e.g., AA:BB:CC:DD:EE:FF)"
              placeholderTextColor="#666"
              autoCapitalize="characters"
              style={{
                borderWidth: 1,
                borderColor: "#ddd",
                borderRadius: 10,
                padding: 12,
                fontSize: 16 * fontScale,
              }}
            />

            <View style={{ flexDirection: "row", gap: 12 }}>
              <Pressable
                onPress={() => setEditOpen(false)}
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

              <Pressable
                onPress={confirmEdit}
                style={{
                  flex: 1,
                  padding: 12,
                  borderRadius: 10,
                  borderWidth: 1,
                  borderColor: "#ddd",
                  alignItems: "center",
                }}
              >
                <Text style={{ fontWeight: "600", fontSize: 16 * fontScale }}>Edit</Text>
              </Pressable>
            </View>
          </View>
        </View>
      </Modal>
    </View>
  );
}