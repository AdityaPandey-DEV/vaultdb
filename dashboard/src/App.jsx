import React, { useState, useEffect, useRef } from 'react';
import { BarChart, Bar, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer, LineChart, Line } from 'recharts';
import { Database, Zap, Activity, HardDrive, Wifi, WifiOff } from 'lucide-react';
import BloomFilterVisualizer from './components/BloomFilterVisualizer';
import WebTerminal from './components/WebTerminal';
import './index.css';

function App() {
  const [data, setData] = useState(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  
  // Live Mode States
  const [isLiveMode, setIsLiveMode] = useState(false);
  const [liveData, setLiveData] = useState(null);
  const [liveError, setLiveError] = useState(null);

  useEffect(() => {
    fetch('/results.json')
      .then((res) => {
        if (!res.ok) throw new Error('Failed to load benchmark results');
        return res.json();
      })
      .then((json) => {
        setData(json);
        setLoading(false);
      })
      .catch((err) => {
        setError(err.message);
        setLoading(false);
      });
  }, []);

  // Live Polling Effect
  useEffect(() => {
    let intervalId;
    
    const fetchLiveStats = async () => {
      try {
        const response = await fetch('http://localhost:5005/api/stats');
        if (!response.ok) throw new Error('API Error');
        const json = await response.json();
        
        if (json.status === 'online') {
          setLiveData(json);
          setLiveError(null);
        } else {
          setLiveError('VaultDB is Offline');
        }
      } catch (err) {
        setLiveError('Connection Refused. Is API Bridge running?');
      }
    };

    if (isLiveMode) {
      fetchLiveStats(); // Fetch immediately
      intervalId = setInterval(fetchLiveStats, 1000); // Then poll every second
    } else {
      setLiveData(null);
      setLiveError(null);
    }

    return () => {
      if (intervalId) clearInterval(intervalId);
    };
  }, [isLiveMode]);

  if (loading) return <div className="loading">Loading benchmark data...</div>;
  if (error) return <div className="error">Error: {error}</div>;
  if (!data) return null;

  // Prepare data for Recharts
  const opsData = [
    {
      name: 'Write (SET)',
      throughput: data.write.ops_per_sec,
    },
    {
      name: 'Read (GET)',
      throughput: data.read.ops_per_sec,
    },
  ];

  const latencyData = [
    {
      name: 'p50',
      Write: data.write.p50_ms,
      Read: data.read.p50_ms,
    },
    {
      name: 'p95',
      Write: data.write.p95_ms,
      Read: data.read.p95_ms,
    },
    {
      name: 'p99',
      Write: data.write.p99_ms,
      Read: data.read.p99_ms,
    },
  ];

  const formatNumber = (num) => new Intl.NumberFormat('en-US').format(num);

  return (
    <div className="dashboard-container">
      <header className="header">
        <div className="header-title-row">
          <h1>VaultDB Benchmark Dashboard</h1>
          <button 
            className={`live-toggle-btn ${isLiveMode ? 'active' : ''}`}
            onClick={() => setIsLiveMode(!isLiveMode)}
          >
            {isLiveMode ? <Wifi size={18} /> : <WifiOff size={18} />}
            {isLiveMode ? 'Live Mode: ON' : 'Live Mode: OFF'}
          </button>
        </div>
        <div className="timestamp">
          {isLiveMode 
            ? <span className={liveError ? 'status-error' : 'status-ok'}>
                {liveError || 'Connected to localhost:6379 via API Bridge'}
              </span>
            : `Last Run: ${new Date(data.timestamp).toLocaleString()}`
          }
        </div>
      </header>

      {/* Conditional rendering for Live Metrics vs Static Benchmark Config */}
      {isLiveMode && liveData ? (
        <div className="metrics-grid live-metrics-grid">
          <div className="metric-card live-card">
            <div className="metric-title">Cache Hit Rate</div>
            <div className="metric-value text-blue">{liveData.cache_hit_rate || 0}<span style={{fontSize: '1rem', color: '#9ca3af'}}>%</span></div>
          </div>
          <div className="metric-card live-card">
            <div className="metric-title">Bloom Filter Saves</div>
            <div className="metric-value text-green">{formatNumber(liveData.bloom_saved || 0)}</div>
          </div>
          <div className="metric-card live-card">
            <div className="metric-title">MemTable Size</div>
            <div className="metric-value text-purple">{((liveData.memtable_bytes || 0) / 1024 / 1024).toFixed(2)} <span style={{fontSize: '1rem', color: '#9ca3af'}}>MB</span></div>
          </div>
          <div className="metric-card live-card">
            <div className="metric-title">Total Keys (Writes)</div>
            <div className="metric-value text-yellow">{formatNumber(liveData.writes || 0)}</div>
          </div>
        </div>
      ) : (
        <div className="metrics-grid">
          <div className="metric-card">
            <div className="metric-title">
              <Zap size={16} style={{ display: 'inline', marginRight: '8px', verticalAlign: 'text-bottom' }} />
              Write Throughput
            </div>
            <div className="metric-value">{formatNumber(data.write.ops_per_sec)} <span style={{fontSize: '1rem', color: '#9ca3af', fontWeight: 'normal'}}>ops/sec</span></div>
          </div>
          <div className="metric-card">
            <div className="metric-title">
              <Activity size={16} style={{ display: 'inline', marginRight: '8px', verticalAlign: 'text-bottom' }} />
              Read Throughput
            </div>
            <div className="metric-value">{formatNumber(data.read.ops_per_sec)} <span style={{fontSize: '1rem', color: '#9ca3af', fontWeight: 'normal'}}>ops/sec</span></div>
          </div>
          <div className="metric-card">
            <div className="metric-title">
              <Database size={16} style={{ display: 'inline', marginRight: '8px', verticalAlign: 'text-bottom' }} />
              Operations Config
            </div>
            <div className="metric-value">{formatNumber(data.config.ops)}</div>
          </div>
          <div className="metric-card">
            <div className="metric-title">
              <HardDrive size={16} style={{ display: 'inline', marginRight: '8px', verticalAlign: 'text-bottom' }} />
              Concurrency
            </div>
            <div className="metric-value">{data.config.threads} <span style={{fontSize: '1rem', color: '#9ca3af', fontWeight: 'normal'}}>threads</span></div>
          </div>
        </div>
      )}

      <div className="charts-grid">
        <div className="chart-panel">
          <h2>Throughput (Ops/sec)</h2>
          <ResponsiveContainer width="100%" height="80%">
            <BarChart data={opsData} margin={{ top: 20, right: 30, left: 20, bottom: 5 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="#1f2937" vertical={false} />
              <XAxis dataKey="name" stroke="#9ca3af" />
              <YAxis stroke="#9ca3af" tickFormatter={(value) => `${value / 1000}k`} />
              <Tooltip 
                cursor={{fill: 'rgba(255, 255, 255, 0.05)'}}
                contentStyle={{ backgroundColor: '#111827', borderColor: '#1f2937', borderRadius: '8px' }}
                itemStyle={{ color: '#3b82f6' }}
              />
              <Bar dataKey="throughput" fill="#3b82f6" radius={[4, 4, 0, 0]} barSize={60} />
            </BarChart>
          </ResponsiveContainer>
        </div>

        <div className="chart-panel">
          <h2>Latency Percentiles (ms)</h2>
          <ResponsiveContainer width="100%" height="80%">
            <LineChart data={latencyData} margin={{ top: 20, right: 30, left: 20, bottom: 5 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="#1f2937" vertical={false} />
              <XAxis dataKey="name" stroke="#9ca3af" />
              <YAxis stroke="#9ca3af" />
              <Tooltip 
                contentStyle={{ backgroundColor: '#111827', borderColor: '#1f2937', borderRadius: '8px' }}
              />
              <Legend wrapperStyle={{ paddingTop: '20px' }} />
              <Line type="monotone" dataKey="Write" stroke="#ef4444" strokeWidth={3} dot={{ r: 6 }} activeDot={{ r: 8 }} />
              <Line type="monotone" dataKey="Read" stroke="#10b981" strokeWidth={3} dot={{ r: 6 }} activeDot={{ r: 8 }} />
            </LineChart>
          </ResponsiveContainer>
        </div>
      </div>

      <WebTerminal isLiveMode={isLiveMode} />

      <BloomFilterVisualizer />

      <div className="architecture-panel">
        <h2>LSM-Tree Architecture Flow</h2>
        <svg viewBox="0 0 800 250" className="arch-diagram" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <marker id="arrowhead" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
              <polygon points="0 0, 10 3.5, 0 7" fill="#9ca3af" />
            </marker>
          </defs>
          
          <rect x="50" y="80" width="120" height="60" rx="8" fill="#1e293b" stroke="#3b82f6" strokeWidth="2" />
          <text x="110" y="115" fill="#f3f4f6" textAnchor="middle" fontWeight="bold">TCP Client</text>
          
          <line x1="170" y1="110" x2="240" y2="110" stroke="#9ca3af" strokeWidth="2" markerEnd="url(#arrowhead)" />
          
          <rect x="250" y="80" width="140" height="60" rx="8" fill="#1e293b" stroke="#10b981" strokeWidth="2" />
          <text x="320" y="115" fill="#f3f4f6" textAnchor="middle" fontWeight="bold">LSM Engine</text>
          
          {/* Write path */}
          <line x1="390" y1="95" x2="480" y2="50" stroke="#ef4444" strokeWidth="2" strokeDasharray="5,5" markerEnd="url(#arrowhead)" />
          <text x="435" y="65" fill="#ef4444" fontSize="12" textAnchor="middle">1. Write</text>
          
          <line x1="390" y1="110" x2="480" y2="110" stroke="#ef4444" strokeWidth="2" markerEnd="url(#arrowhead)" />
          <text x="435" y="105" fill="#ef4444" fontSize="12" textAnchor="middle">2. Write</text>

          {/* Read path */}
          <line x1="390" y1="125" x2="480" y2="185" stroke="#10b981" strokeWidth="2" markerEnd="url(#arrowhead)" />
          <text x="435" y="150" fill="#10b981" fontSize="12" textAnchor="middle">Read</text>
          
          {/* Components */}
          <rect x="490" y="20" width="120" height="50" rx="8" fill="#1e293b" stroke="#f59e0b" strokeWidth="2" />
          <text x="550" y="50" fill="#f3f4f6" textAnchor="middle" fontWeight="bold">WAL</text>
          <text x="550" y="85" fill="#9ca3af" fontSize="12" textAnchor="middle">(Disk Append)</text>
          
          <rect x="490" y="90" width="120" height="50" rx="8" fill="#1e293b" stroke="#8b5cf6" strokeWidth="2" />
          <text x="550" y="120" fill="#f3f4f6" textAnchor="middle" fontWeight="bold">MemTable</text>
          <text x="550" y="155" fill="#9ca3af" fontSize="12" textAnchor="middle">(In-Memory Tree)</text>
          
          <rect x="490" y="170" width="120" height="50" rx="8" fill="#1e293b" stroke="#ec4899" strokeWidth="2" />
          <text x="550" y="200" fill="#f3f4f6" textAnchor="middle" fontWeight="bold">LRU Cache</text>
          <text x="550" y="235" fill="#9ca3af" fontSize="12" textAnchor="middle">(In-Memory Map)</text>
          
          {/* Flush */}
          <line x1="610" y1="115" x2="680" y2="115" stroke="#8b5cf6" strokeWidth="2" markerEnd="url(#arrowhead)" />
          <text x="645" y="105" fill="#8b5cf6" fontSize="12" textAnchor="middle">Flush</text>
          
          <rect x="690" y="50" width="80" height="130" rx="8" fill="#1e293b" stroke="#64748b" strokeWidth="2" />
          <text x="730" y="115" fill="#f3f4f6" textAnchor="middle" fontWeight="bold" transform="rotate(270 730 115)">SSTables (Disk)</text>
          
          {/* Read fallback */}
          <path d="M 610 195 Q 670 195 680 180" fill="none" stroke="#10b981" strokeWidth="2" strokeDasharray="4,4" markerEnd="url(#arrowhead)" />
          <text x="645" y="210" fill="#10b981" fontSize="10" textAnchor="middle">Cache Miss</text>
        </svg>
      </div>
    </div>
  );
}

export default App;
