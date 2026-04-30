import { useEffect, useState } from 'react';
import { Activity, CheckCircle, AlertTriangle, Clock, Settings, Save, RefreshCw, Power } from 'lucide-react';

interface Config {
  moonrakerIP: string;
  moonrakerApiKey: string;
  ledPin: number;
  ledCount: number;
  ledBrightness: number;
  error: { effect: number; color: number; color2: number; speed: number };
  complete: { effect: number; color: number; color2: number; speed: number };
  paused: { effect: number; color: number; color2: number; speed: number };
  standby: { effect: number; color: number; color2: number; speed: number };
  cancelled: { effect: number; color: number; color2: number; speed: number };
  printing: { effect: number; color: number; color2: number; speed: number };
  preparation: { effect: number; color: number; color2: number; speed: number };
}

interface Status {
  state: string;
  progress: number;
  etaSeconds: number;
  connected: boolean;
}

const EFFECTS = [
  { id: 0, name: "Static" },
  { id: 53, name: "Bicolor Chase" },
  { id: 1, name: "Blink" },
  { id: 2, name: "Breath" },
  { id: 31, name: "Chase Color" },
  { id: 33, name: "Chase Rainbow" },
  { id: 3, name: "Color Wipe" },
  { id: 7, name: "Color Wipe Random" },
  { id: 37, name: "Colorful" },
  { id: 44, name: "Comet" },
  { id: 14, name: "Dual Scan" },
  { id: 15, name: "Fade" },
  { id: 48, name: "Fire Flicker" },
  { id: 50, name: "Fire Flicker (Intense)" },
  { id: 49, name: "Fire Flicker (Soft)" },
  { id: 45, name: "Fireworks" },
  { id: 52, name: "Halloween" },
  { id: 43, name: "Larson Scanner" },
  { id: 10, name: "Multi Dynamic" },
  { id: 11, name: "Rainbow" },
  { id: 12, name: "Rainbow Cycle" },
  { id: 8, name: "Random Color" },
  { id: 18, name: "Running Lights" },
  { id: 13, name: "Scan" },
  { id: 9, name: "Single Dynamic" },
  { id: 23, name: "Sparkle" },
  { id: 26, name: "Strobe" },
  { id: 16, name: "Theater Chase" },
  { id: 17, name: "Theater Chase Rainbow" },
  { id: 54, name: "Tricolor Chase" },
  { id: 19, name: "Twinkle" },
  { id: 21, name: "Twinkle Fade" },
  { id: 20, name: "Twinkle Random" },
];

const COLORS = [
  { val: 0x0000FF, name: "Blue" },
  { val: 0x00FFFF, name: "Cyan" },
  { val: 0x101010, name: "Dim White" },
  { val: 0xFFD700, name: "Gold" },
  { val: 0x00FF00, name: "Green" },
  { val: 0x4B0082, name: "Indigo" },
  { val: 0xFF00FF, name: "Magenta" },
  { val: 0xFFA500, name: "Orange" },
  { val: 0xFFC0CB, name: "Pink" },
  { val: 0x800080, name: "Purple" },
  { val: 0xFF0000, name: "Red" },
  { val: 0x40E0D0, name: "Turquoise" },
  { val: 0xFFFFFF, name: "White" },
  { val: 0xFFFF00, name: "Yellow" },
  { val: 0x000000, name: "Black (Off)" },
];

export default function App() {
  const [status, setStatus] = useState<Status>({ state: 'standby', progress: 0, etaSeconds: 0, connected: false });
  const [config, setConfig] = useState<Config | null>(null);
  const [activeTab, setActiveTab] = useState<'dashboard' | 'config'>('dashboard');
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    fetchConfig();
    const interval = setInterval(fetchStatus, 1000);
    return () => clearInterval(interval);
  }, []);

  const fetchStatus = async () => {
    try {
      const res = await fetch('/api/status');
      if (res.ok) setStatus(await res.json());
    } catch (e) {
      // console.error("Error fetching status", e);
    }
  };

  const fetchConfig = async () => {
    try {
      const res = await fetch('/api/config');
      if (res.ok) setConfig(await res.json());
    } catch (e) {
      // console.error("Error fetching config", e);
    }
  };

  const handleSaveConfig = async () => {
    if (!config) return;
    setSaving(true);
    try {
      await fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(config)
      });
      alert('Configuration saved!');
    } catch (e) {
      alert('Failed to save config.');
    }
    setSaving(false);
  };

  const handleRestart = async () => {
    if (confirm("Restart ESP32?")) {
      try {
        await fetch('/api/restart', { method: 'POST' });
        alert('Restarting...');
      } catch (e) {}
    }
  };

  const formatETA = (seconds: number) => {
    if (seconds <= 0) return "Done";
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    return `${h > 0 ? h + 'h ' : ''}${m}m`;
  };

  const renderDashboard = () => (
    <div className="space-y-6">
      <div className="bg-slate-800 p-6 rounded-2xl shadow-xl border border-slate-700 flex items-center justify-between">
        <div>
          <h2 className="text-sm text-slate-400 font-semibold uppercase tracking-wider">Printer State</h2>
          <div className="flex items-center gap-3 mt-1">
            <div className={`w-3 h-3 rounded-full ${status.connected ? 'bg-green-500 shadow-[0_0_10px_rgba(34,197,94,0.6)]' : 'bg-red-500 shadow-[0_0_10px_rgba(239,68,68,0.6)]'}`}></div>
            <span className="text-3xl font-bold capitalize">{status.state}</span>
          </div>
        </div>
        {status.state === 'printing' && <Activity className="text-blue-400 w-12 h-12 animate-pulse" />}
        {status.state === 'complete' && <CheckCircle className="text-green-400 w-12 h-12" />}
        {status.state === 'error' && <AlertTriangle className="text-red-400 w-12 h-12" />}
      </div>

      <div className="bg-slate-800 p-6 rounded-2xl shadow-xl border border-slate-700">
        <div className="flex justify-between items-end mb-4">
          <h2 className="text-xl font-semibold">Progress</h2>
          <span className="text-3xl font-bold text-blue-400">{status.progress.toFixed(1)}%</span>
        </div>
        <div className="w-full bg-slate-900 rounded-full h-6 overflow-hidden border border-slate-700 relative">
          <div 
            className="bg-gradient-to-r from-blue-600 to-cyan-400 h-6 transition-all duration-500 ease-out relative"
            style={{ width: `${status.progress}%` }}
          >
            <div className="absolute inset-0 bg-white/20 w-full h-full animate-[shimmer_2s_infinite] -skew-x-12"></div>
          </div>
        </div>

        <div className="mt-6 flex items-center gap-3 text-slate-300">
          <Clock className="w-5 h-5 text-slate-400" />
          <span className="text-lg">Estimated Time Remaining: <strong className="text-white">{formatETA(status.etaSeconds)}</strong></span>
        </div>
      </div>
    </div>
  );

  const renderConfig = () => {
    if (!config) return <div className="text-center p-10 animate-pulse">Loading Config...</div>;

    const updateStateFX = (stateName: keyof Config, field: string, val: number) => {
      setConfig(prev => {
        if (!prev) return prev;
        const s = prev[stateName] as any;
        return { ...prev, [stateName]: { ...s, [field]: val } };
      });
    };

    return (
      <div className="space-y-6">
        <div className="bg-slate-800 p-6 rounded-2xl border border-slate-700">
          <h2 className="text-xl font-bold mb-4 flex items-center gap-2"><Settings className="w-5 h-5"/> Device Settings</h2>
          <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
            <div>
              <label className="block text-sm font-medium text-slate-400 mb-1">Moonraker IP</label>
              <input type="text" value={config.moonrakerIP} onChange={e => setConfig({...config, moonrakerIP: e.target.value})} className="w-full bg-slate-900 border border-slate-700 rounded-lg p-2 text-white outline-none focus:border-blue-500 transition-colors" />
            </div>
            <div>
              <label className="block text-sm font-medium text-slate-400 mb-1">Moonraker API Key (Optional)</label>
              <input type="password" placeholder="Leave blank if disabled" value={config.moonrakerApiKey || ''} onChange={e => setConfig({...config, moonrakerApiKey: e.target.value})} className="w-full bg-slate-900 border border-slate-700 rounded-lg p-2 text-white outline-none focus:border-blue-500 transition-colors" />
            </div>
            <div>
              <label className="block text-sm font-medium text-slate-400 mb-1">LED Pin</label>
              <input type="number" value={config.ledPin} onChange={e => setConfig({...config, ledPin: parseInt(e.target.value)})} className="w-full bg-slate-900 border border-slate-700 rounded-lg p-2 text-white outline-none focus:border-blue-500 transition-colors" />
            </div>
            <div>
              <label className="block text-sm font-medium text-slate-400 mb-1">LED Count</label>
              <input type="number" value={config.ledCount} onChange={e => setConfig({...config, ledCount: parseInt(e.target.value)})} className="w-full bg-slate-900 border border-slate-700 rounded-lg p-2 text-white outline-none focus:border-blue-500 transition-colors" />
            </div>
            <div>
              <label className="block text-sm font-medium text-slate-400 mb-1">LED Brightness</label>
              <input type="range" min="1" max="255" value={config.ledBrightness} onChange={e => setConfig({...config, ledBrightness: parseInt(e.target.value)})} className="w-full accent-blue-500 mt-2" />
              <div className="text-right text-xs text-slate-500">{config.ledBrightness}/255</div>
            </div>
          </div>
        </div>

        <div className="bg-slate-800 p-6 rounded-2xl border border-slate-700">
          <h2 className="text-xl font-bold mb-4">State Effects Mapping</h2>
          <div className="space-y-4">
            {(['preparation', 'printing', 'paused', 'standby', 'complete', 'error', 'cancelled'] as const).map(state => (
              <div key={state} className="grid grid-cols-1 md:grid-cols-5 gap-4 items-center bg-slate-900 p-3 rounded-xl border border-slate-700/50">
                <span className="capitalize font-semibold text-lg">{state}</span>
                <div className="flex flex-col gap-1">
                  <span className="text-[10px] text-slate-400 uppercase tracking-wider">Effect</span>
                  <select value={config[state].effect} onChange={e => updateStateFX(state, 'effect', parseInt(e.target.value))} className="bg-slate-800 border border-slate-600 rounded-lg p-2 text-white outline-none text-sm">
                    {EFFECTS.map(fx => <option key={fx.id} value={fx.id}>{fx.name}</option>)}
                  </select>
                </div>
                <div className="flex flex-col gap-1">
                  <span className="text-[10px] text-slate-400 uppercase tracking-wider">Color 1</span>
                  <select value={config[state].color} onChange={e => updateStateFX(state, 'color', parseInt(e.target.value))} className="bg-slate-800 border border-slate-600 rounded-lg p-2 text-white outline-none text-sm">
                    {COLORS.map(c => <option key={c.val} value={c.val}>{c.name}</option>)}
                  </select>
                </div>
                <div className="flex flex-col gap-1">
                  <span className="text-[10px] text-slate-400 uppercase tracking-wider">Color 2</span>
                  <select value={config[state].color2} onChange={e => updateStateFX(state, 'color2', parseInt(e.target.value))} className="bg-slate-800 border border-slate-600 rounded-lg p-2 text-white outline-none text-sm">
                    {COLORS.map(c => <option key={c.val} value={c.val}>{c.name}</option>)}
                  </select>
                </div>
                <div className="flex flex-col gap-1">
                  <span className="text-[10px] text-slate-400 uppercase tracking-wider">Speed</span>
                  <input type="range" min="100" max="5000" value={5100 - config[state].speed} onChange={e => updateStateFX(state, 'speed', 5100 - parseInt(e.target.value))} className="w-full accent-blue-500" />
                </div>
              </div>
            ))}
          </div>
        </div>

        <div className="flex justify-end gap-4">
          <button onClick={handleRestart} className="px-4 py-2 bg-slate-700 hover:bg-slate-600 text-white rounded-lg flex items-center gap-2 transition-colors">
            <Power className="w-4 h-4" /> Restart
          </button>
          <button onClick={handleSaveConfig} disabled={saving} className="px-6 py-2 bg-blue-600 hover:bg-blue-500 text-white rounded-lg flex items-center gap-2 font-medium transition-colors shadow-lg shadow-blue-500/20 disabled:opacity-50">
            {saving ? <RefreshCw className="w-4 h-4 animate-spin" /> : <Save className="w-4 h-4" />} Save Configuration
          </button>
        </div>
      </div>
    );
  };

  return (
    <div className="max-w-4xl mx-auto p-4 md:p-8">
      <header className="flex justify-between items-center mb-8">
        <h1 className="text-3xl font-extrabold bg-gradient-to-r from-blue-400 to-cyan-300 bg-clip-text text-transparent">Moonraker Monitor</h1>
        <div className="flex bg-slate-800 rounded-lg p-1 border border-slate-700">
          <button 
            onClick={() => setActiveTab('dashboard')} 
            className={`px-4 py-2 rounded-md font-medium transition-colors ${activeTab === 'dashboard' ? 'bg-blue-600 text-white shadow-lg' : 'text-slate-400 hover:text-white'}`}
          >
            Dashboard
          </button>
          <button 
            onClick={() => setActiveTab('config')} 
            className={`px-4 py-2 rounded-md font-medium transition-colors ${activeTab === 'config' ? 'bg-blue-600 text-white shadow-lg' : 'text-slate-400 hover:text-white'}`}
          >
            Config
          </button>
        </div>
      </header>

      <main>
        {activeTab === 'dashboard' ? renderDashboard() : renderConfig()}
      </main>
    </div>
  );
}
