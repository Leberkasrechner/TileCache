# Tile Caching System for Tileserver-GL

This project consists of two programs:

1. **tile_requester**:  
   Pre-renders raster map tiles for a specified geographic bounding box (default set to Germany) across zoom levels 0 through 17.  
   It queries the Tileserver-GL tile server to generate tiles ahead of time for faster access later.

2. **tile_cache**:  
   An HTTP server that caches requested tiles locally.  
   When a tile is requested:  
   - It first tries to serve the tile from the local cache.  
   - If the tile is missing, it requests the tile from the configured Tileserver-GL, saves it locally, then serves it.  
   This reduces redundant tile rendering and speeds up repeat requests.

---

## Default Configuration

- The **tile_requester** is currently configured to cover Germany approximately with WGS84 bounding box:  
  `min lon=5.53, min lat=47.23, max lon=15.38, max lat=54.96`  
- Zoom levels rendered: 0 to 17  
- You can change these coordinates directly in the source code of the tile_requester to target other regions.

---

## Installation on Ubuntu

1. **Install dependencies:**

```bash
sudo apt update
sudo apt install -y build-essential libboost-all-dev libcurl4-openssl-dev
````

2. **Compile tile\_requester:**

```bash
g++ -std=c++17 -pthread tile_requester.cpp -o tile_requester -lcurl
```

3. **Compile tile\_cache:**

```bash
g++ -std=c++17 -pthread tile_cache.cpp -o tile_cache -lcurl -lboost_system -lboost_thread
```

---

## Usage

### tile\_requester

Run the tile\_requester to pre-render tiles:

```bash
./tile_requester <tile_server_base_url>
```

Example:

```bash
./tile_requester https://tileserver.example.com/styles/osm-bright/
```

This will request all tiles for the configured bounding box and zoom levels.

---

### tile\_cache

Run the tile\_cache server, specifying the tile server base URL (must end with a slash):

```bash
./tile_cache https://tileserver.example.com/styles/osm-bright/
```

The cache server listens on port 8080 and serves tiles at paths like:

```
http://localhost:8080/{zoom}/{x}/{y}.png
```

---

## How it works

* The **tile\_requester** pre-generates tiles by requesting them in bulk and saves local copies.
* The **tile\_cache** serves tiles from local disk if available; if not, it fetches on-demand from the Tileserver-GL, caches, then serves.
* This setup improves performance, reduces load on your tile server, and provides offline resilience.

---

## Notes

* Adjust bounding boxes or zoom levels by editing the tile\_requester source code.
* Make sure the tile server URL is accessible and supports the standard `{z}/{x}/{y}.png` tile URL pattern.
* The system requires sufficient disk space to store cached tiles.
* The cache server is multithreaded and optimized for concurrent requests.

---

Feel free to file issues or request features on GitHub!

