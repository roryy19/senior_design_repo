import { BleManager, Device, Subscription } from 'react-native-ble-plx';
import { BELT_SERVICE_UUID, ALERT_CHAR_UUID, CONFIG_CHAR_UUID } from './uuids';

// --- Types ---

// A beacon alert carries the MAC address of the beacon the belt detected nearby.
// The app looks up the beacon's name from the sensor list by MAC address.
export type AlertPayload =
  | { type: 'beacon'; mac: string }
  | { type: 'obstacle'; directionIndex: number };

export type BleConnectionState = 'disconnected' | 'scanning' | 'connecting' | 'connected';

type StateChangedCb = (state: BleConnectionState) => void;
type AlertReceivedCb = (alert: AlertPayload) => void;

// --- BLE Protocol (must match ESP32 firmware) ---
//
// Alert characteristic (notify):
//   [0x01, B0, B1, B2, B3, B4, B5]  → beacon detected, MAC = B0:B1:B2:B3:B4:B5
//   [0x02, N]                        → obstacle, N = direction index (0=front … 7=front-left)
//
// Config characteristic (write with response):
//   [0x01, N]  → set arm length to N cm (uint8)

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

  private async connectToDevice(device: Device) {
    this.emitState('connecting');
    try {
      const connected = await device.connect({ timeout: 10000 });
      await connected.discoverAllServicesAndCharacteristics();
      this.connectedDevice = connected;

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
        if (error || !characteristic?.value) return;
        const alert = this.decodeAlert(characteristic.value);
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
