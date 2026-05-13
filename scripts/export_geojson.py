import seacharts.enc as senc
import pyproj
from shapely.ops import transform
import json
from pathlib import Path

def convert_to_geojson(config_file, out_path):
    print(f"Loading ENC data using seacharts from config {config_file}...")
    enc = senc.ENC(config_file=config_file, new_data=False)
    
    print("Projecting to WGS84...")
    # UTM 33N is EPSG:32633. 
    project = pyproj.Transformer.from_crs('EPSG:32633', 'EPSG:4326', always_xy=True).transform
    
    features = []
    
    # Process Seabed Depths
    for depth_val, seabed in enc.seabed.items():
        if seabed and not seabed.geometry.is_empty:
            wgs_geom = transform(project, seabed.geometry)
            features.append({
                "type": "Feature",
                "geometry": wgs_geom.__geo_interface__,
                "properties": {
                    "layer": "dybdeareal",
                    "minimumdybde": depth_val,
                }
            })
            
    # Process Land
    if enc.land and not enc.land.geometry.is_empty:
        wgs_geom = transform(project, enc.land.geometry)
        features.append({
            "type": "Feature",
            "geometry": wgs_geom.__geo_interface__,
            "properties": {
                "layer": "landareal",
            }
        })
        
    # Process Shore (Coastline)
    if enc.shore and not enc.shore.geometry.is_empty:
        wgs_geom = transform(project, enc.shore.geometry)
        features.append({
            "type": "Feature",
            "geometry": wgs_geom.__geo_interface__,
            "properties": {
                "layer": "kystkontur",
            }
        })
        
    fc = {
        "type": "FeatureCollection",
        "features": features
    }
    
    print("Saving GeoJSON...")
    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, 'w') as f:
        json.dump(fc, f)
        
    print(f"Successfully exported {len(features)} layers to {out_path}")

if __name__ == '__main__':
    import sys
    if len(sys.argv) == 3:
        convert_to_geojson(sys.argv[1], sys.argv[2])
    else:
        convert_to_geojson('/Users/marine/Code/colav-simulator/config/seacharts.yaml', '/Users/marine/Code/MASS-L3-Tactical Layer/web/public/tiles/enc.geojson')
