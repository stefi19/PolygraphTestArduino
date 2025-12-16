import { useState } from 'react';
import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer
} from 'recharts';
import { Bluetooth, Power, Activity, TrendingUp } from 'lucide-react';

// ---- Type declarations ----
type ChartPoint = {
  time: string;
  value: number;
};

export default function ESP32BLEMonitor() {
  const isWebBluetoothAvailable =
    typeof navigator !== 'undefined' &&
    !!(navigator as any).bluetooth &&
    typeof (navigator as any).bluetooth.requestDevice === 'function';

  const [device, setDevice] = useState<BluetoothDevice | null>(null);
  const [characteristic, setCharacteristic] =
    useState<BluetoothRemoteGATTCharacteristic | null>(null);
  const [connected, setConnected] = useState<boolean>(false);
  const [data, setData] = useState<ChartPoint[]>([]);
  const [currentValue, setCurrentValue] = useState<number>(0);
  const [status, setStatus] = useState<string>('Disconnected');
  const [error, setError] = useState<string>('');
  const [showAllDevices, setShowAllDevices] = useState<boolean>(false);

  const SERVICE_UUID = '4fafc201-1fb5-459e-8fcc-c5c9c331914b';
  const CHARACTERISTIC_UUID = 'beb5483e-36e1-4688-b7f5-ea07361b26a8';

  const connectToDevice = async (): Promise<void> => {
    try {
      if (!isWebBluetoothAvailable) {
        setError(
          'Web Bluetooth API not available. Use Chrome/Edge on macOS and run the app from http://localhost or an https site.'
        );
        setStatus('Unavailable');
        return;
      }
      setError('');
      setStatus('Scanning...');

      // If the user wants to filter, request only devices that match our name/service.
      // Otherwise fall back to acceptAllDevices so the chooser shows everything.
      const options = showAllDevices
        ? { acceptAllDevices: true, optionalServices: [SERVICE_UUID] }
        : {
            filters: [
              { name: 'ESP32_BLE_LED', services: [SERVICE_UUID] },
              { namePrefix: 'ESP32', services: [SERVICE_UUID] },
              { services: [SERVICE_UUID] }
            ]
          };

      // Some TS environments may not perfectly type Bluetooth options; cast to any.
      const device = await (navigator.bluetooth as any).requestDevice(options as any);

      setDevice(device);
      setStatus('Connecting...');

      const server = await device.gatt!.connect();
      const service = await server.getPrimaryService(SERVICE_UUID);
      const char = await service.getCharacteristic(CHARACTERISTIC_UUID);

      setCharacteristic(char);

      await char.startNotifications();
      char.addEventListener(
        'characteristicvaluechanged',
        handleDataReceived
      );

      setConnected(true);
      setStatus('Connected');

      device.addEventListener('gattserverdisconnected', () => {
        setConnected(false);
        setStatus('Disconnected');
        setCharacteristic(null);
      });
    } catch (err) {
      const message =
        err instanceof Error ? err.message : 'Unknown error';
      setError(`Error: ${message}`);
      setStatus('Error');
    }
  };

  const disconnect = (): void => {
    if (device?.gatt?.connected) {
      device.gatt.disconnect();
      setConnected(false);
      setStatus('Disconnected');
      setDevice(null);
      setCharacteristic(null);
    }
  };

  const handleDataReceived = (
    event: Event
  ): void => {
    const target = event.target as BluetoothRemoteGATTCharacteristic;
    const value = target.value;
    if (!value) return;

    let receivedValue: number;

    if (value.byteLength >= 4) {
      receivedValue = value.getFloat32(0, true);
    } else if (value.byteLength >= 2) {
      receivedValue = value.getUint16(0, true);
    } else {
      receivedValue = value.getUint8(0);
    }

    setCurrentValue(receivedValue);

    setData(prev => [
      ...prev.slice(-19),
      {
        time: new Date().toLocaleTimeString(),
        value: receivedValue
      }
    ]);
  };

  const sendData = async (command: string): Promise<void> => {
    if (!characteristic) return;

    try {
      const encoder = new TextEncoder();
      await characteristic.writeValue(encoder.encode(command));
    } catch (err) {
      const message =
        err instanceof Error ? err.message : 'Unknown error';
      setError(`Send error: ${message}`);
    }
  };

  return (
    <div className="min-h-screen bg-slate-900 text-white p-6">
      <h1 className="text-2xl font-bold mb-4 flex items-center gap-2">
        <Bluetooth /> ESP32 BLE Monitor
      </h1>

      <p>Status: {status}</p>

      {error && <p className="text-red-400">{error}</p>}

      {!isWebBluetoothAvailable && (
        <div className="p-4 my-4 bg-yellow-600 text-black rounded">
          <strong>Web Bluetooth unavailable.</strong>
          <div className="mt-2">
            Steps to enable:
            <ul className="list-disc ml-5">
              <li>Use Chrome or Edge on desktop (not Safari/iOS).</li>
              <li>Serve the app from <code>http://localhost</code> or an <code>https://</code> origin.</li>
              <li>Ensure Bluetooth is enabled on your computer and not blocked by OS privacy settings.</li>
            </ul>
          </div>
        </div>
      )}

      <div className="my-3 flex items-center gap-4">
        <label className="flex items-center gap-2 text-sm">
          <input
            type="checkbox"
            checked={showAllDevices}
            onChange={() => setShowAllDevices(s => !s)}
            className="rounded"
          />
          <span>Show all Bluetooth devices (unfiltered)</span>
        </label>

        {!connected ? (
          <button onClick={connectToDevice} disabled={!isWebBluetoothAvailable} className="px-4 py-2 bg-blue-600 rounded disabled:opacity-50">Connect</button>
        ) : (
          <button onClick={disconnect} className="px-4 py-2 bg-red-600 rounded">Disconnect</button>
        )}
      </div>

      <h2 className="text-4xl my-6">{currentValue}</h2>

      <ResponsiveContainer width="100%" height={300}>
        <LineChart data={data}>
          <CartesianGrid strokeDasharray="3 3" />
          <XAxis dataKey="time" />
          <YAxis />
          <Tooltip />
          <Legend />
          <Line dataKey="value" stroke="#3b82f6" />
        </LineChart>
      </ResponsiveContainer>

      {connected && (
        <div className="mt-4">
          <button onClick={() => sendData('ON')}>ON</button>
          <button onClick={() => sendData('OFF')}>OFF</button>
        </div>
      )}
    </div>
  );
}