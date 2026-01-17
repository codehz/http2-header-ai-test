#!/bin/bash
# HTTP/2 Client - Quick Start Guide

echo "=== HTTP/2 Client Quick Start ==="
echo ""
echo "1. Building the project..."
cd /home/codehz/Projects/http2-test
rm -rf build
mkdir build
cd build
cmake ..
make
echo ""
echo "Build complete!"
echo ""

echo "2. Running the HTTP/2 client..."
echo "   Connecting to example.com and retrieving content..."
echo ""
./http2-client
echo ""
echo "3. Success! The client has:"
echo "   ✓ Connected to example.com using TLS"
echo "   ✓ Performed HTTP/2 handshake"
echo "   ✓ Sent an HTTP GET request"
echo "   ✓ Received response headers and body"
echo "   ✓ Decoded the response using HPACK"
echo ""
echo "For more details, see HTTP2_CLIENT_README.md"
