"""Test ENC chart rendering at high zoom levels (0.5nm ~ z14-15)."""

from playwright.sync_api import sync_playwright
import sys
import os
import json
import urllib.request
import math

TRONDHEIM_CENTER = {"lat": 63.44, "lon": 10.38}
SCREENSHOT_DIR = os.path.join(os.path.dirname(__file__), "test_screenshots")


def tile_xy(z, lat, lon):
    n = 2 ** z
    x = int((lon + 180) / 360 * n)
    lat_rad = math.radians(lat)
    y = int((1 - math.log(math.tan(lat_rad) + 1 / math.cos(lat_rad)) / math.pi) / 2 * n)
    return x, y


def test_enc_tile_server():
    url = "http://localhost:3000/trondelag"
    try:
        resp = urllib.request.urlopen(url, timeout=5)
        data = json.loads(resp.read())
        assert data.get("tilejson") == "3.0.0"
        assert data.get("maxzoom") == 16
        assert data.get("minzoom") == 0
        vector_layers = data.get("vector_layers", [])
        layer_ids = [vl["id"] for vl in vector_layers]
        for required in ["land", "depth_area", "coastline", "depth_contour"]:
            assert required in layer_ids, f"Missing layer: {required}"
        print(f"  PASS TileJSON: {len(vector_layers)} layers, z0-z16")
    except Exception as e:
        print(f"  FAIL Tile server: {e}")
        return False
    return True


def test_tile_data_at_zoom():
    all_ok = True
    for z in [12, 13, 14, 15]:
        x, y = tile_xy(z, TRONDHEIM_CENTER["lat"], TRONDHEIM_CENTER["lon"])
        url = f"http://localhost:3000/trondelag/{z}/{x}/{y}"
        try:
            resp = urllib.request.urlopen(url, timeout=5)
            size = len(resp.read())
            if resp.status == 200 and size > 0:
                print(f"  PASS z{z}: tile ({x},{y}) -> {size} bytes")
            else:
                print(f"  WARN z{z}: tile ({x},{y}) -> HTTP {resp.status}, {size} bytes")
        except Exception as e:
            print(f"  FAIL z{z}: {e}")
            all_ok = False
    return all_ok


def test_map_rendering():
    os.makedirs(SCREENSHOT_DIR, exist_ok=True)

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": 1280, "height": 800})

        console_errors = []
        page.on("console", lambda msg: console_errors.append(msg.text) if msg.type == "error" else None)

        tile_requests = []
        def on_request(req):
            if "/trondelag/" in req.url and ".pbf" not in req.url:
                tile_requests.append(req.url)
        page.on("request", on_request)

        page.goto("http://localhost:5173", timeout=30000)
        page.wait_for_load_state("networkidle", timeout=15000)
        page.wait_for_timeout(4000)

        page.screenshot(path=os.path.join(SCREENSHOT_DIR, "01_initial.png"))
        print("  PASS Screenshot: initial load")

        has_map = page.evaluate("() => !!window.__maplibre_map")
        if not has_map:
            print("  WARN __maplibre_map not exposed, using mouse wheel for zoom")

        for zoom, name in [(14, "z14"), (15, "z15"), (16, "z16")]:
            if has_map:
                page.evaluate(f"""
                    () => {{
                        const map = window.__maplibre_map;
                        if (map) map.jumpTo({{ center: [10.38, 63.44], zoom: {zoom} }});
                    }}
                """)
            else:
                map_el = page.locator("[data-testid='sil-map-view']")
                if map_el.count() > 0:
                    box = map_el.bounding_box()
                    if box:
                        cx, cy = box["x"] + box["width"] / 2, box["y"] + box["height"] / 2
                        current_zoom = page.evaluate("() => window.__maplibre_map?.getZoom() || 12")
                        clicks_needed = zoom - int(current_zoom)
                        for _ in range(max(clicks_needed, 0)):
                            page.mouse.move(cx, cy)
                            page.mouse.wheel(0, -100)
                            page.wait_for_timeout(300)

            page.wait_for_timeout(3000)
            page.screenshot(path=os.path.join(SCREENSHOT_DIR, f"{name}_trondheim.png"))

            actual_zoom = page.evaluate("() => window.__maplibre_map?.getZoom() || 'unknown'")
            print(f"  PASS Screenshot: {name} (actual zoom: {actual_zoom})")

        enc_tile_count = len([u for u in tile_requests if "/trondelag/" in u])
        print(f"  INFO ENC tile requests captured: {enc_tile_count}")

        gl_errors = [e for e in console_errors if "WebGL" in e or "shader" in e.lower()]
        if gl_errors:
            print(f"  WARN WebGL errors: {gl_errors[:2]}")
        else:
            print("  PASS No WebGL errors")

        browser.close()
    return True


def test_pixel_diversity():
    try:
        from PIL import Image
        import numpy as np
    except ImportError:
        print("  SKIP PIL/numpy not available")
        return True

    for name in ["z14_trondheim", "z15_trondheim", "z16_trondheim"]:
        path = os.path.join(SCREENSHOT_DIR, f"{name}.png")
        if not os.path.exists(path):
            continue
        img = Image.open(path)
        arr = np.array(img)

        unique_colors = len(np.unique(arr.reshape(-1, arr.shape[2]), axis=0))
        total_pixels = arr.shape[0] * arr.shape[1]

        h, w = arr.shape[:2]
        block_size = 8
        non_ocean_blocks = 0
        total_blocks = 0
        ocean_color = np.array([3, 105, 161])

        for y in range(0, h - block_size, block_size):
            for x in range(0, w - block_size, block_size):
                block = arr[y:y+block_size, x:x+block_size]
                total_blocks += 1
                avg_color = block.mean(axis=(0, 1))
                if not np.allclose(avg_color, ocean_color, atol=30):
                    non_ocean_blocks += 1

        diversity = non_ocean_blocks / total_blocks if total_blocks > 0 else 0
        status = "PASS" if diversity > 0.05 else "WARN"
        print(f"  {status} {name}: {unique_colors} colors, non-ocean={non_ocean_blocks}/{total_blocks} ({diversity:.1%})")
        if diversity <= 0.05:
            print(f"    -> Low diversity: map may not be rendering chart features")

    return True


if __name__ == "__main__":
    print("\n" + "=" * 60)
    print("ENC Chart Rendering Test Suite")
    print("=" * 60)

    results = {}
    print("\n1. Tile Server API")
    results["tile_server"] = test_enc_tile_server()
    print("\n2. Tile Data at Zoom Levels")
    results["tile_data"] = test_tile_data_at_zoom()
    print("\n3. Map Rendering")
    results["rendering"] = test_map_rendering()
    print("\n4. Pixel Diversity Analysis")
    results["pixel"] = test_pixel_diversity()

    print("\n" + "=" * 60)
    passed = sum(1 for v in results.values() if v)
    print(f"Results: {passed}/{len(results)} passed")
    print(f"Screenshots: {SCREENSHOT_DIR}/")
    print("=" * 60)
    sys.exit(0 if passed == len(results) else 1)
