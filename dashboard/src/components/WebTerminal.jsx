import React, { useState, useRef, useEffect } from 'react';
import './WebTerminal.css';

const WebTerminal = ({ isLiveMode }) => {
  const [input, setInput] = useState('');
  const [history, setHistory] = useState([
    { type: 'system', text: 'VaultDB Web Terminal' },
    { type: 'system', text: 'Type HELP for available commands.' },
  ]);
  const endOfHistoryRef = useRef(null);

  useEffect(() => {
    if (!isLiveMode) {
      setHistory(prev => [
        ...prev, 
        { type: 'error', text: 'Terminal is disabled. Turn on "Live Mode" to connect to the backend.' }
      ]);
    } else {
      setHistory(prev => [
        ...prev,
        { type: 'system', text: 'Connected to VaultDB API Bridge.' }
      ]);
    }
  }, [isLiveMode]);

  useEffect(() => {
    endOfHistoryRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [history]);

  const handleSubmit = async (e) => {
    e.preventDefault();
    if (!input.trim()) return;

    const cmd = input.trim();
    setInput('');
    
    // Add command to history
    setHistory(prev => [...prev, { type: 'command', text: cmd }]);

    if (cmd.toUpperCase() === 'CLEAR') {
      setHistory([{ type: 'system', text: 'Terminal cleared.' }]);
      return;
    }

    if (!isLiveMode) {
      setHistory(prev => [...prev, { type: 'error', text: '✗ Live Mode is off. Cannot send command.' }]);
      return;
    }

    try {
      const response = await fetch('http://localhost:5005/api/command', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ command: cmd })
      });
      
      const data = await response.json();
      
      // Colorize response logic based on VaultDB protocol
      let type = 'output';
      if (data.response.startsWith('OK') || data.response.includes('PONG')) {
        type = 'success';
      } else if (data.response.startsWith('ERROR')) {
        type = 'error';
      } else if (data.response.startsWith('VALUE')) {
        type = 'value';
      }

      setHistory(prev => [...prev, { type, text: data.response }]);

    } catch (err) {
      setHistory(prev => [...prev, { 
        type: 'error', 
        text: '✗ Connection refused. Make sure API Bridge (api/dashboard_api.py) is running on port 5005.' 
      }]);
    }
  };

  return (
    <div className="web-terminal-container">
      <div className="web-terminal-header">
        <div className="terminal-dots">
          <span className="dot red"></span>
          <span className="dot yellow"></span>
          <span className="dot green"></span>
        </div>
        <div className="terminal-title">vault-cli (Web)</div>
      </div>
      
      <div className="web-terminal-body">
        <div className="terminal-history">
          {history.map((entry, idx) => (
            <div key={idx} className={`terminal-line line-${entry.type}`}>
              {entry.type === 'command' && <span className="prompt">vaultdb&gt; </span>}
              {entry.type === 'success' && <span className="icon">✓ </span>}
              {entry.type === 'error' && <span className="icon">✗ </span>}
              {entry.type === 'value' && <span className="icon">→ </span>}
              {entry.text.startsWith('VALUE ') ? entry.text.substring(6) : entry.text}
            </div>
          ))}
          <div ref={endOfHistoryRef} />
        </div>
        
        <form onSubmit={handleSubmit} className="terminal-input-form">
          <span className="prompt">vaultdb&gt;</span>
          <input
            type="text"
            value={input}
            onChange={(e) => setInput(e.target.value)}
            disabled={!isLiveMode}
            autoComplete="off"
            spellCheck="false"
            autoFocus
            className="terminal-input"
          />
        </form>
      </div>
    </div>
  );
};

export default WebTerminal;
