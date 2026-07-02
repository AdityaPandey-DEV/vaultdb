import React, { useState, useEffect } from 'react';
import { BarChart, Bar, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer, LineChart, Line } from 'recharts';
import { Database, Zap, Activity, HardDrive, Info } from 'lucide-react';
import './index.css';

const archDetails = {
  default: {
    title: "Interactive Architecture Flow",
    description: "Hover over or click any component in the diagram above to learn how it works under the hood in C++."
  },
  client: {
    title: "TCP Client / Parser",
    description: "Connects via port 6379. The C++ server uses a select() loop for non-blocking I/O. Because TCP is a continuous byte stream, the Connection class buffers bytes until it finds a newline (\\n), then the Parser converts the raw text into a Command struct (e.g., SET, GET)."
  },
  engine: {
    title: "LSM Engine (Coordinator)",
    description: "The core brain of VaultDB. It serializes all operations using a global std::mutex. For writes, it coordinates sending data to both the WAL and MemTable. For reads, it checks the Cache -> MemTable -> SSTables in order. It also lazily checks TTLs to expire old data."
  },
  wal: {
    title: "Write-Ahead Log (WAL)",
    description: "An append-only binary file that ensures data durability. Every SET/DEL is written here FIRST. If the server crashes, RAM is lost, but VaultDB reads the WAL on restart to perfectly rebuild the MemTable. Truncated on flush."
  },
  memtable: {
    title: "MemTable (In-Memory Buffer)",
    description: "Implemented as a C++ std::map (Red-Black Tree), it buffers writes in memory while keeping keys perfectly sorted. Once it hits 4MB, the sorted keys are flushed to disk. It's much faster than writing random disk blocks."
  },
  cache: {
    title: "LRU Cache (O(1) Access)",
    description: "Combines a std::unordered_map with a doubly-linked std::list to keep the most recently accessed keys in memory. Lookups take O(1) time. Cache misses fall back to checking the MemTable and then disk."
  },
  sstable: {
    title: "SSTables (Sorted String Tables)",
    description: "Immutable binary files on disk. Keys are stored in sorted order. Instead of loading the whole file into RAM, a Sparse Index tracks every 100th key's byte offset, allowing extremely fast binary searches directly on the hard drive."
  },
  flush: {
    title: "Flush Operation",
    description: "When the MemTable reaches its 4MB limit, the background thread writes its sorted contents sequentially to a new SSTable file, clears the MemTable, and checkpoints the WAL."
  },
  compaction: {
    title: "Background Compaction",
    description: "As SSTable files pile up, reads get slower (read amplification). A background std::thread occasionally merges older SSTables together (like merge sort), removing deleted keys (tombstones) and keeping only the newest values."
  }
};

function App() {
  const [data, setData] = useState(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [activeBlock, setActiveBlock] = useState('default');

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

  if (loading) return <div className="loading">Loading benchmark data...</div>;
  if (error) return <div className="error">Error: {error}</div>;
  if (!data) return null;

  const opsData = [
    { name: 'Write (SET)', throughput: data.write.ops_per_sec },
    { name: 'Read (GET)', throughput: data.read.ops_per_sec },
  ];

  const latencyData = [
    { name: 'p50', Write: data.write.p50_ms, Read: data.read.p50_ms },
    { name: 'p95', Write: data.write.p95_ms, Read: data.read.p95_ms },
    { name: 'p99', Write: data.write.p99_ms, Read: data.read.p99_ms },
  ];

  const formatNumber = (num) => new Intl.NumberFormat('en-US').format(num);

  const handleInteract = (blockId) => {
    setActiveBlock(blockId);
  };

  const currentDetail = archDetails[activeBlock] || archDetails.default;

  return (
    <div className="dashboard-container">
      <header className="header">
        <h1>VaultDB Benchmark Dashboard</h1>
        <div className="timestamp">
          Last Run: {new Date(data.timestamp).toLocaleString()}
        </div>
      </header>

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

      <div className="architecture-panel">
        <h2>LSM-Tree Architecture Flow</h2>
        
        <svg viewBox="0 0 800 250" className="arch-diagram" xmlns="http://www.w3.org/2000/svg" onMouseLeave={() => handleInteract('default')}>
          <defs>
            <marker id="arrowhead" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
              <polygon points="0 0, 10 3.5, 0 7" fill="#9ca3af" />
            </marker>
          </defs>
          
          {/* Client Block */}
          <g className={`arch-block ${activeBlock === 'client' ? 'active' : ''}`} 
             onMouseEnter={() => handleInteract('client')} 
             onClick={() => handleInteract('client')}>
            <rect x="50" y="80" width="120" height="60" rx="8" fill="#1e293b" stroke="#3b82f6" strokeWidth="2" />
            <text x="110" y="115" fill="#f3f4f6" textAnchor="middle" fontWeight="bold">TCP Client</text>
          </g>
          
          <line x1="170" y1="110" x2="240" y2="110" stroke="#9ca3af" strokeWidth="2" markerEnd="url(#arrowhead)" />
          
          {/* Engine Block */}
          <g className={`arch-block ${activeBlock === 'engine' ? 'active' : ''}`}
             onMouseEnter={() => handleInteract('engine')}
             onClick={() => handleInteract('engine')}>
            <rect x="250" y="80" width="140" height="60" rx="8" fill="#1e293b" stroke="#10b981" strokeWidth="2" />
            <text x="320" y="115" fill="#f3f4f6" textAnchor="middle" fontWeight="bold">LSM Engine</text>
          </g>
          
          {/* Write path */}
          <line x1="390" y1="95" x2="480" y2="50" stroke="#ef4444" strokeWidth="2" strokeDasharray="5,5" markerEnd="url(#arrowhead)" className="arch-path" />
          <text x="435" y="65" fill="#ef4444" fontSize="12" textAnchor="middle">1. Write</text>
          
          <line x1="390" y1="110" x2="480" y2="110" stroke="#ef4444" strokeWidth="2" markerEnd="url(#arrowhead)" className="arch-path" />
          <text x="435" y="105" fill="#ef4444" fontSize="12" textAnchor="middle">2. Write</text>

          {/* Read path */}
          <line x1="390" y1="125" x2="480" y2="185" stroke="#10b981" strokeWidth="2" markerEnd="url(#arrowhead)" className="arch-path" />
          <text x="435" y="150" fill="#10b981" fontSize="12" textAnchor="middle">Read</text>
          
          {/* WAL Block */}
          <g className={`arch-block ${activeBlock === 'wal' ? 'active' : ''}`}
             onMouseEnter={() => handleInteract('wal')}
             onClick={() => handleInteract('wal')}>
            <rect x="490" y="20" width="120" height="50" rx="8" fill="#1e293b" stroke="#f59e0b" strokeWidth="2" />
            <text x="550" y="50" fill="#f3f4f6" textAnchor="middle" fontWeight="bold">WAL</text>
            <text x="550" y="85" fill="#9ca3af" fontSize="12" textAnchor="middle">(Disk Append)</text>
          </g>

          {/* MemTable Block */}
          <g className={`arch-block ${activeBlock === 'memtable' ? 'active' : ''}`}
             onMouseEnter={() => handleInteract('memtable')}
             onClick={() => handleInteract('memtable')}>
            <rect x="490" y="90" width="120" height="50" rx="8" fill="#1e293b" stroke="#8b5cf6" strokeWidth="2" />
            <text x="550" y="120" fill="#f3f4f6" textAnchor="middle" fontWeight="bold">MemTable</text>
            <text x="550" y="155" fill="#9ca3af" fontSize="12" textAnchor="middle">(In-Memory Tree)</text>
          </g>

          {/* LRU Cache Block */}
          <g className={`arch-block ${activeBlock === 'cache' ? 'active' : ''}`}
             onMouseEnter={() => handleInteract('cache')}
             onClick={() => handleInteract('cache')}>
            <rect x="490" y="170" width="120" height="50" rx="8" fill="#1e293b" stroke="#ec4899" strokeWidth="2" />
            <text x="550" y="200" fill="#f3f4f6" textAnchor="middle" fontWeight="bold">LRU Cache</text>
            <text x="550" y="235" fill="#9ca3af" fontSize="12" textAnchor="middle">(In-Memory Map)</text>
          </g>
          
          {/* Flush */}
          <g className={`arch-block ${activeBlock === 'flush' ? 'active' : ''}`}
             onMouseEnter={() => handleInteract('flush')}
             onClick={() => handleInteract('flush')}>
            <line x1="610" y1="115" x2="680" y2="115" stroke="#8b5cf6" strokeWidth="2" markerEnd="url(#arrowhead)" />
            <text x="645" y="105" fill="#8b5cf6" fontSize="12" textAnchor="middle" cursor="pointer">Flush</text>
          </g>
          
          {/* SSTables Block */}
          <g className={`arch-block ${activeBlock === 'sstable' ? 'active' : ''}`}
             onMouseEnter={() => handleInteract('sstable')}
             onClick={() => handleInteract('sstable')}>
            <rect x="690" y="50" width="80" height="130" rx="8" fill="#1e293b" stroke="#64748b" strokeWidth="2" />
            <text x="730" y="115" fill="#f3f4f6" textAnchor="middle" fontWeight="bold" transform="rotate(270 730 115)">SSTables (Disk)</text>
          </g>
          
          {/* Read fallback */}
          <path d="M 610 195 Q 670 195 680 180" fill="none" stroke="#10b981" strokeWidth="2" strokeDasharray="4,4" markerEnd="url(#arrowhead)" />
          <text x="645" y="210" fill="#10b981" fontSize="10" textAnchor="middle">Cache Miss</text>

          {/* Compaction */}
          <g className={`arch-block ${activeBlock === 'compaction' ? 'active' : ''}`}
             onMouseEnter={() => handleInteract('compaction')}
             onClick={() => handleInteract('compaction')}>
            <path d="M 780 80 Q 820 115 780 150" fill="none" stroke="#64748b" strokeWidth="2" markerEnd="url(#arrowhead)" />
            <text x="785" y="120" fill="#64748b" fontSize="10" textAnchor="start">Compact</text>
          </g>
        </svg>

        <div className="info-panel" key={activeBlock}>
          <h3><Info size={20} color={activeBlock === 'default' ? '#9ca3af' : '#3b82f6'} /> {currentDetail.title}</h3>
          <p>{currentDetail.description}</p>
        </div>
      </div>

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
    </div>
  );
}

export default App;
