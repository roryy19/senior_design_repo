// physcial sensors placed
export type PlacedSensor = {
    id: string;
    name: string;
    macAddress?: string; // BLE MAC address of the beacon (e.g. 'AA:BB:CC:DD:EE:FF')
}

// user body dimensions (all stored in cm)
export type UserDimensions = {
    heightCm: number;           // Total height in cm
    groundToBeltCm: number;     // Ground to belt height in cm
    beltToHeadCm: number;       // Calculated: heightCm - groundToBeltCm
    shoulderToFingertipCm: number;  // Arm length from shoulder to fingertip in cm
}