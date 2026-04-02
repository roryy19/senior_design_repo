import { BleManager, Device, Subscription } from 'react-native-ble-plx';
import { BELT_SERVICE_UUID, ALERT_CHAR_UUID, CONFIG_CHAR_UUID } from './uuids';

// --- Types ---

// A beacon alert carries the MAC address of the beacon the belt detected nearby.
// The app looks up the beacon's name from the sensor list by MAC address.
export type AlertPayload =
  | { type: 'beacon'; mac: string }
  | { type: 'obstacle'; directionIndex: number }
  | { type: 'rssi_update'; mac: string; rssi: number };

export type BleConnectionState = 'disconnected' | 'scanning' | 'connecting' | 'connected';

type StateChangedCb = (state: BleConnectionState) => void;
type AlertReceivedCb = (alert: AlertPayload) => void;

// --- BLE Protocol (must match ESP32 firmware) ---
//
// Alert characteristic (notify, belt → phone):
//   [0x01, B0, B1, B2, B3, B4, B5]  → beacon detected, MAC = B0:B1:B2:B3:B4:B5
//   [0x02, N]                        → obstacle, N = direction index (0=front … 7=front-left)
//   [0x03, B0..B5, RSSI_offset]      → RSSI update, RSSI = RSSI_offset - 128
//
// Config characteristic (write, phone → belt):
//   [0x01, N]                                      → set arm length to N cm (uint8)
//   [0x02, MAC[6], IDX_hi, IDX_lo, PCM_DATA...]    → audio clip chunk
//   [0x03, MAC[6]]                                 → audio clip end (finalize storage)
//   [0x04, MAC[6]]                                 → register beacon MAC for scanning
//   [0x05, MAC[6]]                                 → delete beacon (remove + delete clip)

class BleService {
  private manager = new BleManager();
  private connectedDevice: Device | null = null;
  private alertSubscription: Subscription | null = null;
  private disconnectSubscription: Subscription | null = null;
  private scanning = false;
  private scanTimeoutId: ReturnType<typeof setTimeout> | null = null;

  private static readonly SCAN_TIMEOUT_MS = 30_000; // stop scanning after 30 seconds

  private stateChangedCb: StateChangedCb | null = null;
  private alertReceivedCb: AlertReceivedCb | null = null;

  onStateChanged(cb: StateChangedCb) {
    this.stateChangedCb = cb;
  }

  onAlertReceived(cb: AlertReceivedCb) {
    this.alertReceivedCb = cb;
  }

  private emitState(state: BleConnectionState) {
    this.stateChangedCb?.(state);
  }

  // Start scanning for the belt. Waits for Bluetooth to be powered on first.
  startScan() {
    if (this.scanning || this.connectedDevice) return;
    this.scanning = true;
    this.emitState('scanning');

    // onStateChange with emitCurrentValue=true fires immediately with current BT state.
    // We wait for PoweredOn before starting the actual scan.
    const btStateSub = this.manager.onStateChange((btState) => {
      if (btState === 'PoweredOn') {
        btStateSub.remove();
        this.manager.startDeviceScan(
          [BELT_SERVICE_UUID],
          { allowDuplicates: false },
          (error, device) => {
            if (error) {
              console.warn('BLE scan error:', error.message);
              this.stopScanTimeout();
              this.scanning = false;
              this.emitState('disconnected');
              return;
            }
            if (device) {
              this.manager.stopDeviceScan();
              this.stopScanTimeout();
              this.scanning = false;
              this.connectToDevice(device);
            }
          }
        );

        // Stop scanning automatically if no belt is found within the timeout.
        this.scanTimeoutId = setTimeout(() => {
          if (this.scanning) {
            this.manager.stopDeviceScan();
            this.scanning = false;
            this.emitState('disconnected');
          }
        }, BleService.SCAN_TIMEOUT_MS);
      } else if (btState === 'PoweredOff' || btState === 'Unauthorized') {
        btStateSub.remove();
        this.scanning = false;
        this.emitState('disconnected');
      }
    }, true);
  }

  private negotiatedMtu = 23; // default BLE MTU

  private async connectToDevice(device: Device) {
    this.emitState('connecting');
    try {
      const connected = await device.connect({ timeout: 10000 });
      await connected.discoverAllServicesAndCharacteristics();
      this.connectedDevice = connected;

      // Negotiate a larger MTU for faster audio clip transfers.
      // Default MTU is 23 (20 usable). Request 247 for ~235 bytes per write.
      try {
        const mtuDevice = await connected.requestMTU(247);
        this.negotiatedMtu = mtuDevice.mtu ?? 23;
        console.log(`BLE MTU negotiated: ${this.negotiatedMtu}`);
      } catch {
        this.negotiatedMtu = 23;
        console.warn('MTU negotiation failed, using default 23');
      }

      // Watch for unexpected disconnects (e.g. belt turned off)
      this.disconnectSubscription = this.manager.onDeviceDisconnected(
        connected.id,
        () => {
          this.cleanupConnection();
          this.emitState('disconnected');
        }
      );

      this.subscribeToAlerts();
      this.emitState('connected');
    } catch (e) {
      console.warn('BLE connect error:', e);
      this.cleanupConnection();
      this.emitState('disconnected');
    }
  }

  private subscribeToAlerts() {
    if (!this.connectedDevice) return;
    this.alertSubscription = this.connectedDevice.monitorCharacteristicForService(
      BELT_SERVICE_UUID,
      ALERT_CHAR_UUID,
      (error, characteristic) => {
        if (error) {
          console.warn('BLE alert subscription error:', error);
          return;
        }
        if (!characteristic?.value) return;
        console.log('BLE alert received (raw):', characteristic.value);
        const alert = this.decodeAlert(characteristic.value);
        console.log('BLE alert decoded:', alert);
        if (alert) this.alertReceivedCb?.(alert);
      }
    );
  }

  async disconnect() {
    this.manager.stopDeviceScan();
    this.stopScanTimeout();
    this.scanning = false;
    const dev = this.connectedDevice;
    this.cleanupConnection();
    if (dev) {
      try { await dev.cancelConnection(); } catch {}
    }
    this.emitState('disconnected');
  }

  // Send user arm length to the belt so it can scale detection thresholds.
  async sendArmLength(armLengthCm: number) {
    if (!this.connectedDevice) return;
    try {
      const bytes = new Uint8Array([0x01, Math.min(255, Math.round(armLengthCm))]);
      const b64 = btoa(String.fromCharCode(...bytes));
      await this.connectedDevice.writeCharacteristicWithResponseForService(
        BELT_SERVICE_UUID,
        CONFIG_CHAR_UUID,
        b64
      );
    } catch (e) {
      console.warn('BLE write error:', e);
    }
  }

  // Parse a MAC string "AA:BB:CC:DD:EE:FF" into 6 bytes [0xAA, 0xBB, ...].
  private parseMac(mac: string): number[] {
    return mac.split(':').map((h) => parseInt(h, 16));
  }

  // Register a beacon MAC with the belt so it scans for it.
  // MAC is in standard format: "AA:BB:CC:DD:EE:FF".
  async registerBeacon(mac: string) {
    if (!this.connectedDevice) return;
    try {
      const macBytes = this.parseMac(mac);
      const bytes = new Uint8Array([0x04, ...macBytes]);
      const b64 = btoa(String.fromCharCode(...bytes));
      await this.connectedDevice.writeCharacteristicWithResponseForService(
        BELT_SERVICE_UUID,
        CONFIG_CHAR_UUID,
        b64
      );
      console.log(`Registered beacon MAC: ${mac}`);
    } catch (e) {
      console.warn('BLE registerBeacon error:', e);
    }
  }

  // Delete a beacon from the belt (removes from scan list + deletes audio clip).
  async deleteBeacon(mac: string) {
    if (!this.connectedDevice) return;
    try {
      const macBytes = this.parseMac(mac);
      const bytes = new Uint8Array([0x05, ...macBytes]);
      const b64 = btoa(String.fromCharCode(...bytes));
      await this.connectedDevice.writeCharacteristicWithResponseForService(
        BELT_SERVICE_UUID,
        CONFIG_CHAR_UUID,
        b64
      );
      console.log(`Deleted beacon MAC: ${mac}`);
    } catch (e) {
      console.warn('BLE deleteBeacon error:', e);
    }
  }

  // Send an audio clip to the belt for storage in flash.
  // mac: beacon MAC in "AA:BB:CC:DD:EE:FF" format.
  // audioBase64: base64-encoded 8-bit unsigned PCM at 8kHz mono.
  async sendAudioClip(mac: string, audioBase64: string) {
    if (!this.connectedDevice) return;

    const raw = atob(audioBase64);
    const audioBytes = Uint8Array.from(raw, (c) => c.charCodeAt(0));
    const macBytes = this.parseMac(mac);

    // Max payload per write = negotiated MTU - 3 (ATT header)
    // Chunk overhead: 1 (type) + 6 (MAC) + 2 (chunk index) = 9 bytes
    const maxPayload = this.negotiatedMtu - 3;
    const chunkDataSize = Math.max(1, maxPayload - 9);
    const totalChunks = Math.ceil(audioBytes.length / chunkDataSize);

    console.log(
      `Sending audio clip: ${audioBytes.length} bytes, ` +
      `${totalChunks} chunks (${chunkDataSize} bytes/chunk, MTU=${this.negotiatedMtu})`
    );

    try {
      // Send audio data in chunks
      for (let i = 0; i < totalChunks; i++) {
        const offset = i * chunkDataSize;
        const end = Math.min(offset + chunkDataSize, audioBytes.length);
        const chunkData = audioBytes.slice(offset, end);

        // Packet: [0x02, MAC[6], IDX_hi, IDX_lo, PCM_DATA...]
        const packet = new Uint8Array(9 + chunkData.length);
        packet[0] = 0x02;
        packet.set(macBytes, 1);
        packet[7] = (i >> 8) & 0xFF;
        packet[8] = i & 0xFF;
        packet.set(chunkData, 9);

        const b64 = btoa(String.fromCharCode(...packet));
        await this.connectedDevice!.writeCharacteristicWithResponseForService(
          BELT_SERVICE_UUID,
          CONFIG_CHAR_UUID,
          b64
        );
      }

      // Send "audio end" to finalize storage
      const endPacket = new Uint8Array([0x03, ...macBytes]);
      const endB64 = btoa(String.fromCharCode(...endPacket));
      await this.connectedDevice!.writeCharacteristicWithResponseForService(
        BELT_SERVICE_UUID,
        CONFIG_CHAR_UUID,
        endB64
      );

      console.log(`Audio clip sent successfully (${totalChunks} chunks)`);
    } catch (e) {
      console.warn('BLE sendAudioClip error:', e);
      throw e;
    }
  }

  // Returns true if connected to the belt.
  isConnected(): boolean {
    return this.connectedDevice !== null;
  }

  private stopScanTimeout() {
    if (this.scanTimeoutId !== null) {
      clearTimeout(this.scanTimeoutId);
      this.scanTimeoutId = null;
    }
  }

  private cleanupConnection() {
    this.alertSubscription?.remove();
    this.alertSubscription = null;
    this.disconnectSubscription?.remove();
    this.disconnectSubscription = null;
    this.connectedDevice = null;
  }

  private decodeAlert(base64Value: string): AlertPayload | null {
    try {
      const raw = atob(base64Value);
      const bytes = Uint8Array.from(raw, (c) => c.charCodeAt(0));
      if (bytes.length === 0) return null;

      if (bytes[0] === 0x01 && bytes.length >= 7) {
        // Beacon alert: reconstruct MAC address from 6 bytes
        const mac = Array.from(bytes.slice(1, 7))
          .map((b) => b.toString(16).padStart(2, '0').toUpperCase())
          .join(':');
        return { type: 'beacon', mac };
      }

      if (bytes[0] === 0x02 && bytes.length >= 2) {
        return { type: 'obstacle', directionIndex: bytes[1] };
      }

      if (bytes[0] === 0x03 && bytes.length >= 8) {
        const mac = Array.from(bytes.slice(1, 7))
          .map((b) => b.toString(16).padStart(2, '0').toUpperCase())
          .join(':');
        const rssi = bytes[7] - 128;
        return { type: 'rssi_update', mac, rssi };
      }

      return null;
    } catch {
      return null;
    }
  }

  destroy() {
    this.disconnect();
    this.manager.destroy();
  }
}

// Singleton — one BleManager for the entire app lifetime.
export const bleService = new BleService();
