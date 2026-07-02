## 🚀 Deployment & Interview Demo

VaultDB uses a **split deployment** strategy — the dashboard is always online for recruiters to visit, while the C++ engine runs locally to demonstrate systems-level skills.

### 1. Dashboard → Vercel (Free, Always Online)

The React dashboard is a **static site** that reads from a pre-generated `results.json`. No backend needed for the static view. It also includes an **Interactive Bloom Filter Visualizer** that runs entirely in the browser.

**Deploy in 3 steps:**
1. Go to [vercel.com](https://vercel.com) → Sign in with GitHub
2. Click **"Add New Project"** → Import `AdityaPandey-DEV/vaultdb`
3. Set **Root Directory** to `dashboard` → Click **Deploy**

That's it. Vercel auto-detects Vite and builds it. You'll get a URL like `vaultdb-dashboard.vercel.app` to share with recruiters.

> **Re-running benchmarks?** After `bash benchmark/run_benchmark.sh`, the script copies `results.json` into `dashboard/public/`. Commit and push → Vercel auto-redeploys with fresh data.

### 2. Full-Stack Live Demo → Local (Interview)

During an interview, run all 3 services locally to show the full-stack architecture:

```bash
# Terminal 1: Start the C++ Database Engine
./build/vaultdb

# Terminal 2: Start the Python API Bridge (HTTP → TCP)
python3 api/dashboard_api.py

# Terminal 3: Start the React Dashboard
cd dashboard && npm run dev
```

Now open `http://localhost:5173` in your browser:
- Click **"Live Mode: ON"** → the dashboard polls your C++ server every second for real-time stats
- Use the **Web Terminal** to type SET/GET/DEL commands directly in the browser
- Scroll to the **Bloom Filter Visualizer** to demonstrate how the probabilistic data structure works

### 3. Docker (Optional — Portable Demo)

```bash
docker build -t vaultdb .
docker run -p 6379:6379 vaultdb
# Now connect from another terminal: nc localhost 6379
```

### Interview Demo Flow (Recommended)

| Step | What to Show | Why It Impresses |
|------|-------------|-----------------|
| 1 | Open Vercel dashboard URL | "I built a full benchmark visualization pipeline" |
| 2 | Show GitHub repo + commit history | Clean, incremental commits show engineering discipline |
| 3 | Start `./build/vaultdb` + `python3 api/dashboard_api.py` + `npm run dev` | Full-stack: C++ → Python API → React frontend |
| 4 | Click **"Live Mode: ON"** in the dashboard | Real-time metrics prove the full-stack bridge works |
| 5 | Type `SET name aditya` in the **Web Terminal** | "I can interact with my C++ engine directly from the browser" |
| 6 | Run `BENCH 50000` via CLI or Web Terminal | Watch the live metrics cards jump in real-time |
| 7 | Show `STATS` → point out `bloom_saved` | "My Bloom Filter prevented X unnecessary disk reads" |
| 8 | Demo the **Bloom Filter Visualizer** | Type keys, watch bits light up — proves you understand the algorithm |
| 9 | Kill server, restart, `GET` old key | Demonstrate WAL crash recovery |
| 10 | Show test suite: `cd build && ctest` | 24 passing tests = engineering rigor | 
