// physcial sensors placed
export type PlacedSensor = {
    id: string;
    name: string;
}

// user body dimensions (all stored in cm)
export type UserDimensions = {
    heightCm: number;           // Total height in cm
    groundToBeltCm: number;     // Ground to belt height in cm
    beltToHeadCm: number;       // Calculated: heightCm - groundToBeltCm
    frontSensorDistanceAtTouch?: number;  // Optional: calibration value in cm
}