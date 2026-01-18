// Simple WebSocket simulator for the ESP
// Usage: node scripts/sim_ws.js
// Listens on ws://localhost:81 and sends simulated sensor JSON including
// cardId, auth, baseline and heart/gsr/voice/lie values.

import { WebSocketServer } from 'ws';
const port = 81;
const wss = new WebSocketServer({ port });

console.log('Simulator: WebSocket server listening on ws://localhost:' + port);

function randInt(min, max) { return Math.floor(Math.random() * (max - min + 1)) + min; }
function randFloat(min, max, dec=2) { return Math.round((Math.random() * (max-min) + min) * Math.pow(10,dec)) / Math.pow(10,dec); }

wss.on('connection', (ws) => {
  console.log('Simulator: client connected');

  let authorized = false;
  let baseline = false;
  let cardId = null;
  let calibrating = false;
  let calibrationEnd = 0;
  let calibrationDuration = 0;

  // send initial card detection (no auth)
  cardId = 'SIM-1234';
  let t = 0;

  const interval = setInterval(() => {
    t += 200; // ms

    // timeline: 0-3000ms -> auth=0 with cardId
    // 3000-8000ms -> auth=1, baseline false
    // after 8000ms -> baseline true
    if (t < 3000) {
      authorized = false;
    } else if (t >= 3000 && t < 8000) {
      authorized = true;
    } else {
      authorized = true;
      baseline = true;
    }

    // generate sensor values
    const heart = randInt(60, 85);
    const gsr = randInt(100, 600);
    const voice = randInt(0, 1023);

    // simple lie metric: random influenced by heart/gsr
    let lie = Math.min(100, Math.round((Math.abs(heart-72) * 1.2 + (gsr-200)/5 + Math.random()*10)));
    if (!baseline) lie = Math.round(lie/2);

    // compute calibration remaining seconds if calibrating
    let calRem = 0;
    const now = Date.now();
    if (calibrating) {
      if (now >= calibrationEnd) {
        calibrating = false;
        baseline = true;
        calRem = 0;
      } else {
        calRem = Math.ceil((calibrationEnd - now) / 1000);
      }
    }

    const payload = {
      heart,
      gsr,
      voice,
      auth: authorized ? 1 : 0,
      leadsOff: 0,
      lie,
      baseline: baseline ? 1 : 0,
      calibration: calRem,
    };

    if (cardId) payload.cardId = cardId;

    try { ws.send(JSON.stringify(payload)); } catch(e) { /* ignore */ }

  }, 200);

  ws.on('message', (msg) => {
    try {
      const text = msg.toString();
      const obj = JSON.parse(text);
      if (obj && obj.cmd === 'startCalibration') {
        const dur = Number(obj.duration) || 10;
        calibrating = true;
        calibrationDuration = dur * 1000;
        calibrationEnd = Date.now() + calibrationDuration;
        authorized = true; // emulate manual authorize during calibration
        baseline = false;
        // send ack
        try { ws.send(JSON.stringify({ calibrationStarted: dur })); } catch (e) {}
        console.log(`Simulator: calibration started for ${dur}s`);
      }
    } catch (e) {
      // ignore parse errors
    }
  });

  ws.on('close', () => {
    clearInterval(interval);
    console.log('Simulator: client disconnected');
  });
});
