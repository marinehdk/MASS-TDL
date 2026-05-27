import { useState, useCallback, useEffect, useRef } from 'react';
import type { RefObject, Dispatch, SetStateAction } from 'react';
import type maplibregl from 'maplibre-gl';

// ── Types ───────────────────────────────────────────────────────────────

export type DragTarget =
  | { kind: 'vessel'; id: string }
  | { kind: 'wp';     idx: number }
  | { kind: 'cog';    id: string }
  | { kind: 'none' };

export interface DragState {
  active: DragTarget;
  ghostPos: [number, number] | null;
}

export interface WaypointNode {
  idx: number;
  lon: number;
  lat: number;
}

export interface PreviewData {
  ownShip?: { lat: number; lon: number; heading: number; sog?: number; cog?: number };
  targets?: Array<{ id: string; lat: number; lon: number; heading: number; sog?: number; cog?: number }>;
}

export interface MapInteractionOptions {
  mapRef: RefObject<maplibregl.Map | null>;
  previewData: PreviewData | null;
  onYamlPatch: (path: string, value: unknown) => void;
  initialWpNodes?: WaypointNode[];
}

export interface MapInteractionReturn {
  dragState: DragState;
  wpNodes: WaypointNode[];
  setWpNodes: Dispatch<SetStateAction<WaypointNode[]>>;
}

// ── Hit-test helpers ────────────────────────────────────────────────────

const VESSEL_HIT_RADIUS_PX = 20;
const WP_HIT_RADIUS_PX = 15;
const COG_HIT_RADIUS_PX = 12;

// ── Hook ─────────────────────────────────────────────────────────────────

export function useMapInteraction(opts: MapInteractionOptions): MapInteractionReturn {
  const { mapRef, previewData, onYamlPatch, initialWpNodes } = opts;

  const [dragState, setDragState] = useState<DragState>({ active: { kind: 'none' }, ghostPos: null });
  const [wpNodes, setWpNodes] = useState<WaypointNode[]>(initialWpNodes || []);
  const dragActiveRef = useRef<DragTarget>({ kind: 'none' });

  useEffect(() => {
    if (initialWpNodes) setWpNodes(initialWpNodes);
  }, [initialWpNodes]);

  const handleMouseDown = useCallback((e: maplibregl.MapMouseEvent) => {
    const map = mapRef.current;
    if (!map) return;
    const point = e.point;

    // 1. Check WP nodes
    for (const wp of wpNodes) {
      const px = map.project([wp.lon, wp.lat]);
      const dx = point.x - px.x;
      const dy = point.y - px.y;
      if (Math.sqrt(dx * dx + dy * dy) < WP_HIT_RADIUS_PX) {
        const target: DragTarget = { kind: 'wp', idx: wp.idx };
        dragActiveRef.current = target;
        setDragState({ active: target, ghostPos: null });
        map.getCanvas().style.cursor = 'grabbing';
        e.preventDefault();
        return;
      }
    }

    // 2. Check vessel markers
    const vessels: Array<{ id: string; lon: number; lat: number }> = [];
    if (previewData?.ownShip) {
      vessels.push({ id: 'ownship', lon: previewData.ownShip.lon, lat: previewData.ownShip.lat });
    }
    if (previewData?.targets) {
      for (const t of previewData.targets) {
        vessels.push({ id: t.id, lon: t.lon, lat: t.lat });
      }
    }
    for (const v of vessels) {
      const px = map.project([v.lon, v.lat]);
      const dx = point.x - px.x;
      const dy = point.y - px.y;
      if (Math.sqrt(dx * dx + dy * dy) < VESSEL_HIT_RADIUS_PX) {
        const target: DragTarget = { kind: 'vessel', id: v.id };
        dragActiveRef.current = target;
        setDragState({ active: target, ghostPos: null });
        map.getCanvas().style.cursor = 'grabbing';
        e.preventDefault();
        return;
      }
    }

    // 3. COG line endpoints — compute tip position from vessel position + heading
    const checkCog = (lon: number, lat: number, cogDeg: number, sogMs: number) => {
      if (sogMs <= 0) return false;
      const distNm = Math.max((sogMs * 360) / 1852, 0.5);
      const cogRad = cogDeg * Math.PI / 180;
      const cosLat = Math.cos(lat * Math.PI / 180) || 1e-9;
      const tipLon = lon + (distNm / 60 * Math.sin(cogRad)) / cosLat;
      const tipLat = lat + (distNm / 60 * Math.cos(cogRad));
      const tipPx = map.project([tipLon, tipLat]);
      const dx = point.x - tipPx.x;
      const dy = point.y - tipPx.y;
      return Math.sqrt(dx * dx + dy * dy) < COG_HIT_RADIUS_PX;
    };

    if (previewData?.ownShip) {
      const os = previewData.ownShip;
      if (checkCog(os.lon, os.lat, os.heading, (os.sog ?? 0) / 1.94384)) {
        const target: DragTarget = { kind: 'cog', id: 'ownship' };
        dragActiveRef.current = target;
        setDragState({ active: target, ghostPos: null });
        map.getCanvas().style.cursor = 'grabbing';
        e.preventDefault();
        return;
      }
    }
    if (previewData?.targets) {
      for (const t of previewData.targets) {
        if (checkCog(t.lon, t.lat, t.heading, (t.sog ?? 0) / 1.94384)) {
          const target: DragTarget = { kind: 'cog', id: t.id };
          dragActiveRef.current = target;
          setDragState({ active: target, ghostPos: null });
          map.getCanvas().style.cursor = 'grabbing';
          e.preventDefault();
          return;
        }
      }
    }

    dragActiveRef.current = { kind: 'none' };
  }, [mapRef, previewData, wpNodes]);

  const handleMouseMove = useCallback((e: maplibregl.MapMouseEvent) => {
    const active = dragActiveRef.current;
    if (active.kind === 'none') return;
    setDragState(prev => ({ ...prev, ghostPos: [e.lngLat.lng, e.lngLat.lat] }));
  }, []);

  const handleMouseUp = useCallback((e: maplibregl.MapMouseEvent) => {
    const map = mapRef.current;
    const active = dragActiveRef.current;
    if (!map || active.kind === 'none') return;

    const lon = Number(e.lngLat.lng.toFixed(6));
    const lat = Number(e.lngLat.lat.toFixed(6));

    switch (active.kind) {
      case 'vessel': {
        if (active.id === 'ownship') {
          onYamlPatch('ownShip.initial.position.latitude', lat);
          onYamlPatch('ownShip.initial.position.longitude', lon);
        } else {
          const tgtIdx = previewData?.targets?.findIndex(t => t.id === active.id) ?? -1;
          if (tgtIdx >= 0) {
            onYamlPatch(`targetShips.${tgtIdx}.initial.position.latitude`, lat);
            onYamlPatch(`targetShips.${tgtIdx}.initial.position.longitude`, lon);
          }
        }
        break;
      }
      case 'wp': {
        onYamlPatch(`voyageTask.waypoints.${active.idx}.lat`, lat);
        onYamlPatch(`voyageTask.waypoints.${active.idx}.lon`, lon);
        setWpNodes(prev => prev.map(wp =>
          wp.idx === active.idx ? { ...wp, lat, lon } : wp
        ));
        break;
      }
      case 'cog': {
        let vesselLon: number, vesselLat: number;
        if (active.id === 'ownship') {
          vesselLon = previewData?.ownShip?.lon ?? 0;
          vesselLat = previewData?.ownShip?.lat ?? 0;
        } else {
          const t = previewData?.targets?.find(tgt => tgt.id === active.id);
          vesselLon = t?.lon ?? 0;
          vesselLat = t?.lat ?? 0;
        }
        const dLon = lon - vesselLon;
        const dLat = lat - vesselLat;
        const bearingDeg = ((Math.atan2(dLon, dLat) * 180 / Math.PI) + 360) % 360;
        const heading = Number(bearingDeg.toFixed(1));

        if (active.id === 'ownship') {
          onYamlPatch('ownShip.initial.heading', heading);
        } else {
          const tgtIdx = previewData?.targets?.findIndex(t => t.id === active.id) ?? -1;
          if (tgtIdx >= 0) {
            onYamlPatch(`targetShips.${tgtIdx}.initial.heading`, heading);
          }
        }
        break;
      }
    }

    dragActiveRef.current = { kind: 'none' };
    setDragState({ active: { kind: 'none' }, ghostPos: null });
    map.getCanvas().style.cursor = '';
  }, [mapRef, previewData, onYamlPatch]);

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;

    map.on('mousedown', handleMouseDown);
    map.on('mousemove', handleMouseMove);
    map.on('mouseup', handleMouseUp);

    return () => {
      map.off('mousedown', handleMouseDown);
      map.off('mousemove', handleMouseMove);
      map.off('mouseup', handleMouseUp);
      map.getCanvas().style.cursor = '';
    };
  }, [mapRef, handleMouseDown, handleMouseMove, handleMouseUp]);

  return { dragState, wpNodes, setWpNodes };
}