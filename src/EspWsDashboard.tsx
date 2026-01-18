import React, { useRef, useState, useEffect } from 'react';
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

type Point = { time: string; value: number };

export default function EspWsDashboard() {
  const [espIp, setEspIp] = useState<string>(() => {
    try {
      return localStorage.getItem('espIp') || '';
    } catch (e) {
      return '';
    }
  });
  const [connected, setConnected] = useState(false);
  const [status, setStatus] = useState<string>('Disconnected');
  const [heart, setHeart] = useState<number>(0);
  const [gsr, setGsr] = useState<number>(0);
  const [voice, setVoice] = useState<number>(0);
  const [lie, setLie] = useState<number>(0);
  const [baseline, setBaseline] = useState<boolean>(false);
  const [authorized, setAuthorized] = useState<boolean>(false);
  const [lastCard, setLastCard] = useState<string | null>(null);
  const [calibrating, setCalibrating] = useState<boolean>(false);
  const [calibrationSeconds, setCalibrationSeconds] = useState<number>(0);

  const [heartData, setHeartData] = useState<Point[]>([]);
  const [gsrData, setGsrData] = useState<Point[]>([]);
  const [voiceData, setVoiceData] = useState<Point[]>([]);
  const [events, setEvents] = useState<string[]>([]);
  const [recording, setRecording] = useState<boolean>(false);
  const [recordedRows, setRecordedRows] = useState<Array<{time:string, heart:number, gsr:number, voice:number, lie:number, cardId?:string}>>([]);
  const [toast, setToast] = useState<string | null>(null);
  const toastTimerRef = useRef<number | null>(null);

  const wsRef = useRef<WebSocket | null>(null);

  useEffect(() => {
    return () => {
      if (wsRef.current) {
        wsRef.current.close();
      }
    };
  }, []);

  useEffect(() => {
    try {
      if (espIp) localStorage.setItem('espIp', espIp);
    } catch (e) {
      // ignore
    }
  }, [espIp]);

  function pushPoint(setter: React.Dispatch<React.SetStateAction<Point[]>>, value: number) {
    setter(prev => {
      const next = [...prev, { time: new Date().toLocaleTimeString(), value }];
      if (next.length > 100) next.shift();
      return next;
    });
  }

  const connect = () => {
    if (!espIp) {
      setStatus('Set ESP IP first');
      return;
    }

    const url = `ws://${espIp}:81`;
    setStatus('Connecting...');

    try {
      const ws = new WebSocket(url);
      wsRef.current = ws;

      ws.onopen = () => {
        setConnected(true);
        setStatus('Connected');
        addEvent('WebSocket opened');
      };

      ws.onmessage = ev => {
        try {
          const data = JSON.parse(ev.data);
          // If the ESP acknowledges starting calibration it may send { calibrationStarted: N }
          if (typeof data.calibrationStarted !== 'undefined') {
            const dur = Number(data.calibrationStarted) || 10;
            setCalibrating(true);
            setCalibrationSeconds(dur);
            setStatus('Calibrating');
            addEvent(`ESP: calibration started ${dur}s`);
            // show inline ack toast
            if (toastTimerRef.current) window.clearTimeout(toastTimerRef.current);
            setToast(`Calibration started (${dur}s)`);
            toastTimerRef.current = window.setTimeout(() => setToast(null), 3500);
            // continue parsing periodic frames if present
          }
          // If ESP provides a calibration countdown field in periodic frames, follow it
          if (typeof data.calibration !== 'undefined') {
            const cal = Number(data.calibration) || 0;
            setCalibrationSeconds(cal);
            setCalibrating(cal > 0);
            if (cal === 0 && data.baseline) {
              setCalibrating(false);
              setAuthorized(true);
              addEvent('Calibration complete — baseline established (ESP)');
              setStatus('Authorized');
              if (toastTimerRef.current) window.clearTimeout(toastTimerRef.current);
              setToast('Calibration complete');
              toastTimerRef.current = window.setTimeout(() => setToast(null), 3000);
            }
          }
          // parse numeric/safe values
          const h = Number(data.heart || 0);
          const g = Number(data.gsr || 0);
          const v = Number(data.voice || 0);
          const l = Number(data.lie || 0);
          const b = Boolean(Number(data.baseline));
          const a = Boolean(Number(data.auth || 0));

          setHeart(h);
          setGsr(g);
          setVoice(v);
          setLie(l);
          setBaseline(b);

          // optional card id
          if (data.cardId) {
            setLastCard(String(data.cardId));
            addEvent(`Card detected: ${data.cardId}`);
          }

          // authorization: when auth arrives, start calibration then enable plotting
          // When authorization arrives, either let the ESP drive calibration (if it provides
          // the `calibration` field/ack) or fall back to a local 10s countdown.
          if (a && !authorized && !calibrating) {
            if (typeof data.calibration !== 'undefined') {
              // ESP is driving calibration; we've already handled its countdown above.
              addEvent('Authorization received — ESP will drive calibration');
              setStatus('Calibrating');
            } else {
              // start a 10-second local calibration period
              setCalibrating(true);
              setCalibrationSeconds(10);
              addEvent('Authorization received — starting 10s calibration (local)');
              setStatus('Calibrating');

              // countdown
              const intervalId = setInterval(() => {
                setCalibrationSeconds(s => {
                  if (s <= 1) {
                    clearInterval(intervalId);
                    setCalibrating(false);
                    setAuthorized(true);
                    addEvent('Calibration complete — starting plot (local)');
                    setStatus('Authorized');
                    return 0;
                  }
                  return s - 1;
                });
              }, 1000);
            }
          }

          // if auth is lost while calibrating, cancel calibration
          // If auth lost while a local calibration was active, cancel it.
          if (!a && calibrating && typeof data.calibration === 'undefined') {
            setCalibrating(false);
            setCalibrationSeconds(0);
            addEvent('Calibration cancelled (auth lost)');
            setStatus(connected ? 'Connected' : 'Disconnected');
          }

          // If we're recording, save a row (only if authorized)
          if (authorized && recording) {
            setRecordedRows(prev => [{
              time: new Date().toISOString(),
              heart: h, gsr: g, voice: v, lie: l,
              cardId: data.cardId ? String(data.cardId) : undefined
            }, ...prev].slice(0, 10000));
          }

          // if not authorized yet, don't push to chart
          if (authorized) {
            pushPoint(setHeartData, h);
            pushPoint(setGsrData, g);
            pushPoint(setVoiceData, v);
          }
        } catch (e) {
          console.error('Invalid JSON', e);
        }
      };

      ws.onclose = () => {
        setConnected(false);
        setStatus('Disconnected');
      };

      ws.onerror = err => {
        console.error('WebSocket error', err);
        setStatus('Connection error');
      };
    } catch (e) {
      setStatus('Connection failed');
    }
  };

  const disconnect = () => {
    wsRef.current?.close();
    wsRef.current = null;
    setConnected(false);
    setStatus('Disconnected');
  };

  function addEvent(msg: string) {
    setEvents(prev => {
      const next = [`${new Date().toLocaleTimeString()} ${msg}`, ...prev].slice(0, 20);
      return next;
    });
  }

  const resetAuthorization = () => {
    setAuthorized(false);
    setHeartData([]);
    setGsrData([]);
    setVoiceData([]);
    addEvent('Authorization reset');
    setStatus(connected ? 'Connected' : 'Disconnected');
  };

  return (
    <div className="min-h-screen bg-slate-900 text-white p-6">
      <div className="max-w-5xl mx-auto">
        <header className="flex items-center justify-between mb-6">
          <h1 className="text-3xl font-extrabold">Polygraph Monitor</h1>
          <div className="text-sm text-slate-300">Status: <span className="font-medium text-white">{status}</span></div>
        </header>
        {/* Inline toast ack */}
        {toast && (
          <div className="fixed top-6 right-6 bg-black/70 text-white px-4 py-2 rounded shadow-lg z-50">
            {toast}
          </div>
        )}

        <div className="mb-4 flex gap-2 items-center">
          <input
            placeholder="ESP IP (e.g. 192.168.1.42)"
            value={espIp}
            onChange={e => setEspIp(e.target.value)}
            className="px-3 py-2 rounded text-black"
          />
          {!connected ? (
            <button onClick={connect} className="px-4 py-2 bg-emerald-500 rounded">Connect</button>
          ) : (
            <button onClick={disconnect} className="px-4 py-2 bg-red-600 rounded">Disconnect</button>
          )}
          <button onClick={resetAuthorization} className="ml-3 px-3 py-2 bg-yellow-600 rounded">Reset Auth</button>
        </div>

        {/* Authorization / waiting / calibration UI */}
        {connected && !authorized && (
          <div className="border-2 border-dashed border-slate-600 rounded-lg p-8 mb-6 text-center">
            {!calibrating ? (
              <>
                <div className="text-2xl font-bold mb-2">Waiting for authorization</div>
                <div className="text-slate-300 mb-4">Please present the authorization card to the Arduino/reader.</div>
                <div className="inline-block p-6 bg-slate-800 rounded animate-pulse">
                  <svg xmlns="http://www.w3.org/2000/svg" className="h-12 w-12 text-amber-400 mx-auto" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                    <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={1.5} d="M3 7v10a2 2 0 002 2h14a2 2 0 002-2V7M16 3H8v4h8V3z" />
                  </svg>
                  <div className="mt-3 text-sm text-slate-300">{lastCard ? `Last card: ${lastCard}` : 'No card detected yet'}</div>
                </div>
                <div className="mt-4 text-sm text-slate-400">When the device grants authorization the plots will begin.</div>
                <div className="mt-4">
                  <button
                    onClick={() => {
                      if (!wsRef.current || wsRef.current.readyState !== WebSocket.OPEN) {
                        addEvent('Cannot request calibration: WS not connected');
                        return;
                      }
                      try {
                        wsRef.current.send(JSON.stringify({ cmd: 'startCalibration', duration: 10 }));
                        addEvent('Requested ESP calibration (10s)');
                      } catch (e) {
                        addEvent('Failed to send calibration request');
                      }
                    }}
                    className="mt-2 px-4 py-2 bg-indigo-600 rounded"
                  >
                    Request ESP calibration (10s)
                  </button>
                </div>
              </>
            ) : (
              <>
                <div className="text-2xl font-bold mb-2">Calibrating — {calibrationSeconds}s</div>
                <div className="text-slate-300 mb-4">Please remain still. Calibration in progress...</div>
                <div className="inline-block p-6 bg-slate-800 rounded">
                  <svg xmlns="http://www.w3.org/2000/svg" className="h-12 w-12 text-emerald-400 mx-auto" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                    <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={1.5} d="M12 8v4l3 3" />
                  </svg>
                </div>
                <div className="mt-4 text-sm text-slate-400">Calibration will finish and the plots will begin automatically.</div>
              </>
            )}
          </div>
        )}

        {/* Main dashboard once authorized */}
        {(!connected || (connected && authorized)) && (
          <>
            <div className="flex items-center justify-center flex-col mb-6">
              {/* Radial gauge */}
              <div style={{width:180, height:180, position:'relative'}}>
                <svg viewBox="0 0 36 36" style={{width:'100%', height:'100%'}}>
                  <path d="M18 2.0845a15.9155 15.9155 0 1 1 0 31.831" fill="none" stroke="#1f2937" strokeWidth="2" />
                  <path
                    stroke="#f59e0b"
                    strokeWidth="2.5"
                    strokeLinecap="round"
                    fill="none"
                    strokeDasharray={`${lie.toFixed(0)}, 100`}
                    d="M18 2.0845a15.9155 15.9155 0 1 1 0 31.831"
                  />
                </svg>
                <div style={{position:'absolute', inset:0, display:'flex', alignItems:'center', justifyContent:'center', flexDirection:'column'}}>
                  <div className="text-4xl font-extrabold text-amber-300">{lie.toFixed(0)}%</div>
                  <div className="text-xs text-slate-400">Lie probability</div>
                </div>
              </div>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-3 gap-4 mb-6">
              <div className="bg-white text-black p-4 rounded shadow">
                <div className="text-sm">Heart</div>
                <div className="text-2xl font-bold">{heart}</div>
              </div>
              <div className="bg-white text-black p-4 rounded shadow">
                <div className="text-sm">GSR</div>
                <div className="text-2xl font-bold">{gsr}</div>
              </div>
              <div className="bg-white text-black p-4 rounded shadow">
                <div className="text-sm">Voice</div>
                <div className="text-2xl font-bold">{voice}</div>
              </div>
            </div>
            <div className="flex items-center gap-3 mb-4">
              {!recording ? (
                <button onClick={() => { setRecording(true); addEvent('Recording started'); }} className="px-4 py-2 bg-green-600 rounded">Start Test</button>
              ) : (
                <button onClick={() => { setRecording(false); addEvent('Recording stopped'); }} className="px-4 py-2 bg-red-600 rounded">Stop Test</button>
              )}

              <button onClick={() => {
                // Download CSV
                const rows = recordedRows.slice().reverse();
                if (rows.length === 0) { addEvent('No recorded rows to export'); return; }
                const csv = ["time,heart,gsr,voice,lie,cardId", ...rows.map(r => `${r.time},${r.heart},${r.gsr},${r.voice},${r.lie},${r.cardId||''}`)].join('\n');
                const blob = new Blob([csv], {type: 'text/csv'});
                const url = URL.createObjectURL(blob);
                const a = document.createElement('a');
                a.href = url;
                a.download = `polygraph-recording-${new Date().toISOString().slice(0,19)}.csv`;
                document.body.appendChild(a);
                a.click();
                a.remove();
                URL.revokeObjectURL(url);
                addEvent('CSV exported');
              }} className="px-4 py-2 bg-sky-600 rounded">Export CSV</button>

              <div className="ml-auto text-sm text-slate-300">Recorded rows: {recordedRows.length}</div>
            </div>
            {/* Charts: only show data when authorized */}
            <div style={{ width: '100%', height: 300 }} className="mb-6">
              <ResponsiveContainer>
                <LineChart data={authorized ? heartData : []}>
                  <CartesianGrid strokeDasharray="3 3" />
                  <XAxis dataKey="time" hide />
                  <YAxis />
                  <Tooltip />
                  <Line dataKey="value" stroke="#dc3545" dot={false} isAnimationActive={false} />
                </LineChart>
              </ResponsiveContainer>
            </div>

            <div style={{ width: '100%', height: 300 }} className="mb-6">
              <ResponsiveContainer>
                <LineChart data={authorized ? gsrData : []}>
                  <CartesianGrid strokeDasharray="3 3" />
                  <XAxis dataKey="time" hide />
                  <YAxis />
                  <Tooltip />
                  <Line dataKey="value" stroke="#28a745" dot={false} isAnimationActive={false} />
                </LineChart>
              </ResponsiveContainer>
            </div>

            <div style={{ width: '100%', height: 300 }} className="mb-6">
              <ResponsiveContainer>
                <LineChart data={authorized ? voiceData : []}>
                  <CartesianGrid strokeDasharray="3 3" />
                  <XAxis dataKey="time" hide />
                  <YAxis />
                  <Tooltip />
                  <Line dataKey="value" stroke="#007bff" dot={false} isAnimationActive={false} />
                </LineChart>
              </ResponsiveContainer>
            </div>
          </>
        )}

        {/* Event log */}
        <div className="mt-6">
          <h3 className="text-sm text-slate-300 mb-2">Event log</h3>
          <div className="bg-slate-800 p-3 rounded max-h-40 overflow-auto text-xs text-slate-300">
            {events.length === 0 ? <div className="text-slate-500">No events yet</div> : (
              <ul>
                {events.map((e, i) => <li key={i} className="mb-1">{e}</li>)}
              </ul>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
