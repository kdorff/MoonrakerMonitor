/**
 * @file App.tsx
 * @brief Main React application for the Moonraker Monitor web dashboard.
 * 
 * Provides real-time printer status visualization and a comprehensive 
 * configuration interface for managing LED effects and device settings.
 * 
 * Uses React hooks for state management and Lucide for iconography.
 * The UI is styled with Tailwind CSS for a premium, responsive feel.
 */

import { useEffect, useState } from 'react';
import { Activity, CheckCircle, AlertTriangle, Clock, Settings, Save, RefreshCw, Power } from 'lucide-react';

/**
 * @interface Config
 * @brief Represents the global application configuration structure.
 * Matches the AppConfig struct in the C++ backend.
 */
interface Config {
  moonrakerIP: string;
  moonrakerApiKey: string;
  ledPin: number;
  ledCount: number;
  ledBrightness: number;
  // State-specific LED configurations. Each state maps to an effect, speed, and 2 colors.
  error: { effect: number; color: number; color2: number; speed: number };
  complete: { effect: number; color: number; color2: number; speed: number };
  paused: { effect: number; color: number; color2: number; speed: number };
  standby: { effect: number; color: number; color2: number; speed: number };
  cancelled: { effect: number; color: number; color2: number; speed: number };
  printing: { effect: number; color: number; color2: number; speed: number };
  preparation: { effect: number; color: number; color2: number; speed: number };
  disconnected: { effect: number; color: number; color2: number; speed: number };
  ledType: number; // Bitmask for Adafruit_NeoPixel type (e.g. GRB, RGBW)
}

/**
 * @interface Status
 * @brief Represents the current printer status returned by the backend API.
 */
interface Status {
  state: string;       ///< Current state string (printing, standby, complete, etc.)
  progress: number;    ///< Percent progress (0-100)
  etaSeconds: number;  ///< Estimated time remaining in seconds
  connected: boolean;  ///< Whether the ESP32 is successfully talking to Moonraker
}

// Supported WS2812FX effects. IDs match the C++ enum values.
// IDs 72-75 are custom ported WLED effects.
const EFFECTS = [
  { id: 0, name: "Static" },
  { id: 53, name: "Bicolor Chase" },
  { id: 1, name: "Blink" },
  { id: 2, name: "Breath" },
  { id: 31, name: "Chase Color" },
  { id: 33, name: "Chase Rainbow" },
  { id: 75, name: "Chunchun (WLED)" },
  { id: 3, name: "Color Wipe" },
  { id: 7, name: "Color Wipe Random" },
  { id: 37, name: "Colorful" },
  { id: 72, name: "Colorloop (WLED)" },
  { id: 44, name: "Comet" },
  { id: 14, name: "Dual Scan" },
  { id: 15, name: "Fade" },
  { id: 48, name: "Fire Flicker" },
  { id: 50, name: "Fire Flicker (Intense)" },
  { id: 49, name: "Fire Flicker (Soft)" },
  { id: 45, name: "Fireworks" },
  { id: 52, name: "Halloween" },
  { id: 74, name: "Lake (WLED)" },
  { id: 43, name: "Larson Scanner" },
  { id: 10, name: "Multi Dynamic" },
  { id: 11, name: "Rainbow" },
  { id: 73, name: "Rainbow (WLED)" },
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

// Mapping of Adafruit_NeoPixel constants to human-readable names.
// These bitmasks tell the hardware how to interpret the data stream.
const LED_TYPES = [
  { val: 82, name: "WS2812B / WS2811 (GRB)" },
  { val: 6, name: "WS2811 / Some WS2812 (RGB)" },
  { val: 88, name: "WS2811 (BRG)" },
  { val: 210, name: "SK6812 (GRBW)" },
  { val: 198, name: "SK6812 (RGBW)" },
];

// Curated color palette for easy selection. 
// Values are 32-bit hex (0xRRGGBB).
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
  // Application State
  const [status, setStatus] = useState<Status>({ state: 'standby', progress: 0, etaSeconds: 0, connected: false });
  const [config, setConfig] = useState<Config | null>(null);
  const [activeTab, setActiveTab] = useState<'dashboard' | 'config'>('dashboard');
  const [saving, setSaving] = useState(false);

  /**
   * @brief Initialize application.
   * Fetches the config once and starts a 1-second polling interval for status.
   */
  useEffect(() => {
    fetchConfig();
    const interval = setInterval(fetchStatus, 1000);
    return () => clearInterval(interval);
  }, []);

  /**
   * @brief Retrieve current printer status from the ESP32 API.
   * Target: GET /api/status
   */
  const fetchStatus = async () => {
    try {
      const res = await fetch('/api/status');
      if (res.ok) setStatus(await res.json());
    } catch (e) {
      // Fail silently to prevent UI noise during temporary disconnections
    }
  };

  /**
   * @brief Retrieve device configuration from the ESP32 API.
   * Target: GET /api/config
   */
  const fetchConfig = async () => {
    try {
      const res = await fetch('/api/config');
      if (res.ok) setConfig(await res.json());
    } catch (e) {
      console.error("Error fetching config", e);
    }
  };

  /**
   * @brief Submit updated configuration to the ESP32.
   * Target: POST /api/config
   */
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

  /**
   * @brief Request a system restart of the ESP32.
   * Target: POST /api/restart
   */
  const handleRestart = async () => {
    if (confirm("Restart ESP32?")) {
      try {
        await fetch('/api/restart', { method: 'POST' });
        alert('Restarting...');
      } catch (e) { }
    }
  };

  /**
   * @brief Formats seconds into a human-readable duration (e.g., "1h 23m").
   * 
   * @param seconds Total seconds to format.
   * @return string Formatted duration.
   */
  const formatETA = (seconds: number) => {
    if (seconds <= 0) return "Done";
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    return `${h > 0 ? h + 'h ' : ''}${m}m`;
  };

  /**
   * @brief Calculates local completion time based on remaining seconds.
   * Uses the client's system clock for localization.
   * 
   * @param seconds Seconds remaining until completion.
   * @return string Formatted absolute time (e.g. "Sat, 4:30 PM").
   */
  const formatCompletionTime = (seconds: number) => {
    if (seconds <= 0) return "";
    const finishTime = new Date(Date.now() + seconds * 1000);
    return finishTime.toLocaleString([], { weekday: 'short', hour: 'numeric', minute: '2-digit' });
  };

  /**
   * @brief Renders the Dashboard tab.
   * Includes state card, progress bar with shimmer effect, and ETA reporting.
   */
  const renderDashboard = () => (
    <div className="space-y-6">
      {/* State Card - Shows active printer state and connection heartbeat */}
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
        {status.state === 'disconnected' && <Power className="text-slate-500 w-12 h-12" />}
      </div>

      {/* Progress Card - Interactive progress bar and linear tracking */}
      <div className="bg-slate-800 p-6 rounded-2xl shadow-xl border border-slate-700">
        <div className="flex justify-between items-end mb-4">
          <h2 className="text-xl font-semibold">Progress</h2>
          <span className="text-3xl font-bold text-blue-400">{status.progress.toFixed(1)}%</span>
        </div>
        {/* Shimmer Effect Progress Bar: Uses a CSS animation on a skew-x-12 overlay to simulate movement */}
        <div className="w-full bg-slate-900 rounded-full h-6 overflow-hidden border border-slate-700 relative">
          <div
            className="bg-gradient-to-r from-blue-600 to-cyan-400 h-6 transition-all duration-500 ease-out relative"
            style={{ width: `${status.progress}%` }}
          >
            <div className="absolute inset-0 bg-white/20 w-full h-full animate-[shimmer_2s_infinite] -skew-x-12"></div>
          </div>
        </div>

        {/* ETA Section - On-board calculated time remaining and localized finish time */}
        <div className="mt-6 flex items-center justify-between text-slate-300">
          <div className="flex items-center gap-3">
            <Clock className="w-5 h-5 text-slate-400" />
            <span className="text-lg">Time Remaining: <strong className="text-white">{formatETA(status.etaSeconds)}</strong></span>
          </div>
          {status.etaSeconds > 0 && status.state === 'printing' && (
            <div className="text-lg text-slate-400">
              Finishes at <strong className="text-white">{formatCompletionTime(status.etaSeconds)}</strong>
            </div>
          )}
        </div>
      </div>
    </div>
  );

  /**
   * @brief Renders the Config tab.
   * Includes hardware settings, network settings, and LED state mappings.
   */
  const renderConfig = () => {
    if (!config) return <div className="text-center p-10 animate-pulse">Loading Config...</div>;

    /**
     * @brief Updates a nested state configuration property (effect, color, speed).
     */
    const updateStateFX = (stateName: keyof Config, field: string, val: number) => {
      setConfig(prev => {
        if (!prev) return prev;
        const s = prev[stateName] as any;
        return { ...prev, [stateName]: { ...s, [field]: val } };
      });
    };

    return (
      <div className="space-y-6">
        {/* Hardware & Network Settings */}
        <div className="bg-slate-800 p-6 rounded-2xl border border-slate-700">
          <h2 className="text-xl font-bold mb-4 flex items-center gap-2"><Settings className="w-5 h-5" /> Device Settings</h2>
          <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
            <div>
              <label className="block text-sm font-medium text-slate-400 mb-1">Moonraker IP</label>
              <input type="text" value={config.moonrakerIP} onChange={e => setConfig({ ...config, moonrakerIP: e.target.value })} className="w-full bg-slate-900 border border-slate-700 rounded-lg p-2 text-white outline-none focus:border-blue-500 transition-colors" />
            </div>
            <div>
              <label className="block text-sm font-medium text-slate-400 mb-1">Moonraker API Key (Optional)</label>
              <input type="password" placeholder="Leave blank if disabled" value={config.moonrakerApiKey || ''} onChange={e => setConfig({ ...config, moonrakerApiKey: e.target.value })} className="w-full bg-slate-900 border border-slate-700 rounded-lg p-2 text-white outline-none focus:border-blue-500 transition-colors" />
            </div>
            <div>
              <label className="block text-sm font-medium text-slate-400 mb-1">LED Pin</label>
              <input type="number" value={config.ledPin} onChange={e => setConfig({ ...config, ledPin: parseInt(e.target.value) })} className="w-full bg-slate-900 border border-slate-700 rounded-lg p-2 text-white outline-none focus:border-blue-500 transition-colors" />
            </div>
            <div>
              <label className="block text-sm font-medium text-slate-400 mb-1">LED Count</label>
              <input type="number" value={config.ledCount} onChange={e => setConfig({ ...config, ledCount: parseInt(e.target.value) })} className="w-full bg-slate-900 border border-slate-700 rounded-lg p-2 text-white outline-none focus:border-blue-500 transition-colors" />
            </div>
            <div>
              <label className="block text-sm font-medium text-slate-400 mb-1">LED Brightness</label>
              <input type="range" min="1" max="255" value={config.ledBrightness} onChange={e => setConfig({ ...config, ledBrightness: parseInt(e.target.value) })} className="w-full accent-blue-500 mt-2" />
              <div className="text-right text-xs text-slate-500">{config.ledBrightness}/255</div>
            </div>
            <div>
              <label className="block text-sm font-medium text-slate-400 mb-1">LED Strip Type</label>
              <select value={config.ledType} onChange={e => setConfig({ ...config, ledType: parseInt(e.target.value) })} className="w-full bg-slate-900 border border-slate-700 rounded-lg p-2 text-white outline-none focus:border-blue-500 transition-colors">
                {LED_TYPES.map(type => <option key={type.val} value={type.val}>{type.name}</option>)}
              </select>
            </div>
          </div>
        </div>

        {/* Animation Mapping Settings - Defines how the LEDs react to printer state changes */}
        <div className="bg-slate-800 p-6 rounded-2xl border border-slate-700">
          <h2 className="text-xl font-bold mb-4">State Effects Mapping</h2>
          <div className="space-y-4">
            {(['preparation', 'printing', 'paused', 'standby', 'complete', 'error', 'cancelled', 'disconnected'] as const).map(state => (
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
                  {/* Speed is inverted in UI for UX: Slider RIGHT = Lower ms = FASTER animation */}
                  <input type="range" min="100" max="5000" value={5100 - config[state].speed} onChange={e => updateStateFX(state, 'speed', 5100 - parseInt(e.target.value))} className="w-full accent-blue-500" />
                </div>
              </div>
            ))}
          </div>
        </div>

        {/* Action Buttons */}
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

  /**
   * @brief Main Render Wrapper.
   * Handles tab navigation and layout.
   */
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
