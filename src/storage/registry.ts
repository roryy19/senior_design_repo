import AsyncStorage from "@react-native-async-storage/async-storage";
import { PlacedSensor } from "../domain/types";
import { jsx } from "react/jsx-runtime";

const KEY = "placed_sensors_v1"

// note - promise = type-safe way to handle async ops, value doesnt have to be present
export async function loadSensors(): Promise<PlacedSensor[]> {
    // get list of sensors
    const raw = await AsyncStorage.getItem(KEY);
    if (!raw) return [];

    // convert to JSON string and return parsed result
    try {
        const parsed = JSON.parse(raw) as PlacedSensor[];
        return Array.isArray(parsed) ? parsed : [];
    } catch {
        return [];
    }
}

// overwrite with entire list of current sensors
export async function saveSensors(sensors: PlacedSensor[]): Promise<void> {
    await AsyncStorage.setItem(KEY, JSON.stringify(sensors));
}

// add new sensor to list
export async function addSensor(newSensor: PlacedSensor): Promise<PlacedSensor[]> {
    const current = await loadSensors();
    
    // create new array with newSensor as first element, then include all other elments (current)
    const next = [newSensor, ...current];
    await saveSensors(next);
    // return updated list
    return next; 
}

// update existing sensor via ID
export async function updateSensor(updated: PlacedSensor): Promise<PlacedSensor[]> {
    const current = await loadSensors();

    // loop through all sensors to find matching ID --> replace with new info (ID stays the same)
    const next = current.map((s) => (s.id == updated.id ? updated : s));
    await saveSensors(next);
    return next;
}

export async function removeSensor(id: string): Promise<PlacedSensor[]> {
    const current = await loadSensors();
    
    // filter keeps only the ones that match condition, so keeps all but sensor with ID to remove
    const next = current.filter((s) => s.id != id);
    await saveSensors(next);
    return next;
}
