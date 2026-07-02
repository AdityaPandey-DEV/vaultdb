import React, { useState } from 'react';
import './BloomFilter.css';

// Replicating the C++ Hash Functions in JS for the UI
function hash1(str) {
  let h = 2166136261;
  for (let i = 0; i < str.length; i++) {
    h ^= str.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return (h >>> 0) % 64;
}

function hash2(str) {
  let h = 123456789;
  for (let i = 0; i < str.length; i++) {
    h ^= str.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return (h >>> 0) % 64;
}

function hash3(str) {
  let h = 5381;
  for (let i = 0; i < str.length; i++) {
    h = ((h << 5) + h) ^ str.charCodeAt(i);
  }
  return (h >>> 0) % 64;
}

const BloomFilterVisualizer = () => {
  const [filterBits, setFilterBits] = useState(new Array(64).fill(false));
  const [inputValue, setInputValue] = useState('');
  const [addedKeys, setAddedKeys] = useState([]);
  const [activeBits, setActiveBits] = useState([]);
  const [checkResult, setCheckResult] = useState(null);

  const handleAdd = () => {
    if (!inputValue) return;
    const bit1 = hash1(inputValue);
    const bit2 = hash2(inputValue);
    const bit3 = hash3(inputValue);

    setFilterBits(prev => {
      const newBits = [...prev];
      newBits[bit1] = true;
      newBits[bit2] = true;
      newBits[bit3] = true;
      return newBits;
    });

    setAddedKeys(prev => [...new Set([...prev, inputValue])]);
    setActiveBits([bit1, bit2, bit3]);
    setCheckResult('Added!');
  };

  const handleCheck = () => {
    if (!inputValue) return;
    const bit1 = hash1(inputValue);
    const bit2 = hash2(inputValue);
    const bit3 = hash3(inputValue);
    
    setActiveBits([bit1, bit2, bit3]);

    if (filterBits[bit1] && filterBits[bit2] && filterBits[bit3]) {
      setCheckResult('Might Contain (Possibly false positive)');
    } else {
      setCheckResult('Definitely NOT Present (Disk read saved!)');
    }
  };

  const handleClear = () => {
    setFilterBits(new Array(64).fill(false));
    setAddedKeys([]);
    setActiveBits([]);
    setInputValue('');
    setCheckResult(null);
  };

  return (
    <div className="bloom-container">
      <div className="bloom-header">
        <h2>Interactive Bloom Filter</h2>
        <p>A simulation of the C++ probabilistic data structure used in VaultDB SSTables.</p>
      </div>

      <div className="bloom-controls">
        <input 
          type="text" 
          value={inputValue}
          onChange={(e) => setInputValue(e.target.value)}
          placeholder="Enter a key..."
          className="bloom-input"
        />
        <button onClick={handleAdd} className="btn-add">Add to Filter</button>
        <button onClick={handleCheck} className="btn-check">Check Key</button>
        <button onClick={handleClear} className="btn-clear">Reset</button>
      </div>

      {checkResult && (
        <div className={`bloom-result ${checkResult.includes('NOT') ? 'result-no' : 'result-yes'}`}>
          Result: <strong>{checkResult}</strong>
        </div>
      )}

      <div className="bit-grid">
        {filterBits.map((isSet, index) => {
          const isActive = activeBits.includes(index);
          return (
            <div 
              key={index} 
              className={`bit-box ${isSet ? 'bit-set' : ''} ${isActive ? 'bit-active' : ''}`}
            >
              {index}
            </div>
          );
        })}
      </div>

      {addedKeys.length > 0 && (
        <div className="bloom-keys">
          <strong>Keys stored in filter: </strong>
          {addedKeys.join(', ')}
        </div>
      )}
    </div>
  );
};

export default BloomFilterVisualizer;
