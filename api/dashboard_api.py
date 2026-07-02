#!/usr/bin/env python3
"""
VaultDB API Bridge

A lightweight HTTP server that connects the React Dashboard to the C++ VaultDB TCP server.
Runs on port 5000 and forwards requests to port 6379.
Uses only the Python Standard Library (no Flask/Django required).
"""

import socket
import json
import logging
from http.server import BaseHTTPRequestHandler, HTTPServer
import urllib.parse

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

VAULTDB_HOST = "localhost"
VAULTDB_PORT = 6379
API_PORT = 5000

def send_to_vaultdb(command: str) -> str:
    """Send a command to the C++ TCP server and return the response."""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2.0)
        sock.connect((VAULTDB_HOST, VAULTDB_PORT))
        sock.sendall((command + "\n").encode())
        response = sock.recv(4096).decode().strip()
        sock.close()
        return response
    except Exception as e:
        logging.error(f"Failed to connect to VaultDB: {e}")
        return f"ERROR Connection failed: {e}"

class VaultDBAPIHandler(BaseHTTPRequestHandler):
    
    def _send_cors_headers(self):
        """Sets headers to allow the React dev server to communicate with this API."""
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header("Access-Control-Allow-Headers", "X-Requested-With, Content-type")

    def do_OPTIONS(self):
        """Handle CORS preflight requests."""
        self.send_response(200, "ok")
        self._send_cors_headers()
        self.end_headers()

    def do_GET(self):
        """Handle GET requests (e.g., /api/stats)"""
        if self.path == '/api/stats':
            response = send_to_vaultdb("STATS")
            
            # If server is down, return error JSON
            if response.startswith("ERROR"):
                self.send_response(503)
                self._send_cors_headers()
                self.send_header('Content-type', 'application/json')
                self.end_headers()
                self.wfile.write(json.dumps({"status": "offline", "error": response}).encode())
                return
                
            # Parse the STATS string into a JSON object
            # Example: "STATS writes=10 reads=5 cache_hits=2 ..."
            stats_dict = {"status": "online"}
            if response.startswith("STATS"):
                parts = response[6:].split()
                for part in parts:
                    if "=" in part:
                        key, val = part.split("=")
                        # Convert to number if possible, removing '%' if present
                        val = val.replace("%", "")
                        try:
                            stats_dict[key] = float(val) if '.' in val else int(val)
                        except ValueError:
                            stats_dict[key] = val
            
            self.send_response(200)
            self._send_cors_headers()
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps(stats_dict).encode())
            
        else:
            self.send_response(404)
            self._send_cors_headers()
            self.end_headers()
            self.wfile.write(b"Not Found")

    def do_POST(self):
        """Handle POST requests (e.g., /api/command) for the Web Terminal"""
        if self.path == '/api/command':
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length).decode('utf-8')
            
            try:
                body = json.loads(post_data)
                command = body.get('command', '').strip()
                
                if not command:
                    response_text = "ERROR Empty command"
                else:
                    response_text = send_to_vaultdb(command)
                
                self.send_response(200)
                self._send_cors_headers()
                self.send_header('Content-type', 'application/json')
                self.end_headers()
                self.wfile.write(json.dumps({"response": response_text}).encode())
                
            except json.JSONDecodeError:
                self.send_response(400)
                self._send_cors_headers()
                self.end_headers()
                self.wfile.write(json.dumps({"response": "ERROR Invalid JSON"}).encode())
        else:
            self.send_response(404)
            self._send_cors_headers()
            self.end_headers()

def run():
    server_address = ('', API_PORT)
    httpd = HTTPServer(server_address, VaultDBAPIHandler)
    logging.info(f"VaultDB API Bridge running on http://localhost:{API_PORT}")
    logging.info("Press Ctrl+C to stop.")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    httpd.server_close()
    logging.info("API Bridge stopped.")

if __name__ == '__main__':
    run()
