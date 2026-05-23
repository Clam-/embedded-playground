import { SerialPort, ReadlineParser } from 'serialport';
import { WebSocketServer, WebSocket } from 'ws';

const WS_PORT = 8081;
const BAUD_RATE = 115200;

async function detectArduinoPort(): Promise<string | null> {
  const ports = await SerialPort.list();
  const arduino = ports.find(
    (p) =>
      p.manufacturer?.toLowerCase().includes('arduino') ||
      p.manufacturer?.toLowerCase().includes('wch') ||
      p.manufacturer?.toLowerCase().includes('ftdi') ||
      p.vendorId === '2341' ||
      p.vendorId === '1b4f' ||
      p.path.includes('usbmodem') ||
      p.path.includes('usbserial'),
  );
  return arduino?.path ?? null;
}

async function main() {
  const portArg = process.argv[2];
  let portPath = portArg;

  if (!portPath) {
    console.log('[serial] Auto-detecting Arduino port...');
    portPath = await detectArduinoPort();
    if (!portPath) {
      console.error('[serial] No Arduino found. Available ports:');
      const ports = await SerialPort.list();
      for (const p of ports) {
        console.error(`  ${p.path} (${p.manufacturer ?? 'unknown'})`);
      }
      console.error('[serial] Specify a port: pnpm dev /dev/ttyXXX');
      process.exit(1);
    }
  }

  console.log(`[serial] Connecting to ${portPath} at ${BAUD_RATE} baud`);

  const serial = new SerialPort({ path: portPath, baudRate: BAUD_RATE });
  const parser = serial.pipe(new ReadlineParser({ delimiter: '\r\n' }));

  const wss = new WebSocketServer({ port: WS_PORT });
  console.log(`[serial] WebSocket server on ws://localhost:${WS_PORT}`);

  const clients = new Set<WebSocket>();
  let labels: string[] | null = null;
  let isFirstLine = true;

  wss.on('connection', (ws) => {
    clients.add(ws);
    console.log(`[serial] Client connected (${clients.size} total)`);
    if (labels) {
      ws.send(JSON.stringify({ type: 'labels', data: labels }));
    }
    ws.on('close', () => {
      clients.delete(ws);
      console.log(`[serial] Client disconnected (${clients.size} total)`);
    });
  });

  function broadcast(msg: object) {
    const json = JSON.stringify(msg);
    for (const client of clients) {
      if (client.readyState === WebSocket.OPEN) {
        client.send(json);
      }
    }
  }

  parser.on('data', (line: string) => {
    const trimmed = line.trim();
    if (!trimmed) return;

    const parts = trimmed.split(',').map((s) => s.trim());

    if (isFirstLine) {
      labels = parts;
      isFirstLine = false;
      console.log(`[serial] Labels: ${labels.join(', ')}`);
      broadcast({ type: 'labels', data: labels });
      return;
    }

    const values = parts.map(Number);
    if (values.some((v) => isNaN(v))) return;

    broadcast({ type: 'data', data: values, t: Date.now() });
  });

  serial.on('error', (err) => {
    console.error(`[serial] Error: ${err.message}`);
    process.exit(1);
  });

  serial.on('open', () => {
    console.log('[serial] Port opened');
  });
}

main();
