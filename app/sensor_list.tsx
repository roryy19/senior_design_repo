import React, { use, useEffect, useState } from "react";
import { View, Text, FlatList, Pressable, Modal, TextInput, Alert } from "react-native";
import { Stack } from "expo-router";

import type { PlacedSensor } from "../src/domain/types";
import { loadSensors, saveSensors, updateSensor, removeSensor } from "../src/storage/registry";

// ID generator for sensors
function makeId(): string {
  return Math.random().toString(36).slice(2, 10);
}

export default function SensorListScreen() {
  // React state: the list shown in the UI
  // sensors: array of sensors
  // setSensors: function to replace sensors with this new value then re-render screen
  const [sensors, setSensors] = useState<PlacedSensor[]>([]);

  // UI state for "Add Sensor" modal
  const [addOpen, setAddOpen] = useState(false);
  const [newName, setNewName] = useState("");

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

  // Open add modal
  function openAdd() {
    setNewName("");
    setAddOpen(true);
  }

  // Add a sensor (update state + persist)
  async function confirmAdd() {
    const name = newName.trim();
    if (!name) return;

    const newSensor: PlacedSensor = {
      id: makeId(),
      name,
    };

    const next = [newSensor, ...sensors];
    setSensors(next);
    await saveSensors(next);
    setAddOpen(false);
  }

  // Open edit modal
  function openEdit(sensor: PlacedSensor) {
    setEditingSensor(sensor); // sensor that is being edited
    setNewName(sensor.name); // prefill input with current name
    setEditOpen(true);
  }

  // Edit name of existing sensor
  async function confirmEdit() {
    const name = newName.trim();
    if (!name || !editingSensor) return;

    // create new sensor with all field the same except name
    const updated: PlacedSensor = {...editingSensor, name};
    
    const next = await updateSensor(updated);
    setSensors(next);
    
    setEditOpen(false);
    setEditingSensor(null);
  }

  function confirmDelete(sensor: PlacedSensor) {
    Alert.alert(
      "Delete sensor?",
      `Are you sure you want to delete "${sensor.name}"?`,
      [
        {text : "Cancel", style: "cancel"},
        {
          text: "Delete",
          style: "destructive",
          onPress: async () => {
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
        <Text style={{ fontSize: 18, fontWeight: "600" }}>{item.name}</Text>

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
            <Text style={{ color: "black", fontWeight: "600" }}>Edit</Text>
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
            <Text style={{ color: "white", fontWeight: "600" }}>Delete</Text>
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
          title: "Sensors",
          headerRight: () => (
            <Pressable onPress={openAdd} style={{ paddingHorizontal: 12, paddingVertical: 6 }}>
              <Text style={{ fontSize: 16 }}>Add</Text>
            </Pressable>
          ),
        }}
      />

      {/* The vertical list */}
      <FlatList
        data={sensors}
        keyExtractor={(item) => item.id}
        contentContainerStyle={{ padding: 16 }}
        ItemSeparatorComponent={() => <View style={{ height: 10 }} />}
        ListEmptyComponent={() => (
          <Text style={{ opacity: 0.6 }}>
            No sensors yet. Tap “Add” to create one.
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
            <Text style={{ fontSize: 20, fontWeight: "700" }}>Add sensor</Text>

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
                fontSize: 16,
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
                <Text>Cancel</Text>
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
                <Text style={{ fontWeight: "600" }}>Add</Text>
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
            <Text style={{ fontSize: 20, fontWeight: "700" }}>Edit sensor</Text>

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
                fontSize: 16,
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
                <Text>Cancel</Text>
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
                <Text style={{ fontWeight: "600" }}>Edit</Text>
              </Pressable>
            </View>
          </View>
        </View>
      </Modal>
    </View>
  );
}