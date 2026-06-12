export interface AisTarget {
  target_id: number;
  lat: number;
  lon: number;
  sog_kn: number | null;
  cog_deg: number | null;
  heading_deg?: number | null;
  source_sensor: 'ais';
  ship_name?: string | null;
  ship_type?: string | null;
  destination?: string | null;
  nav_status?: string | null;
  received_at_utc?: string | null;
  vessel_length_m?: number | null;
  vessel_beam_m?: number | null;
}

export interface AisLatestResponse {
  provider: string;
  generated_at_utc: string;
  target_count: number;
  targets: AisTarget[];
}

export async function fetchLatestAisTargets(
  baseUrl = '/ais-twin',
  region?: string,
): Promise<AisLatestResponse> {
  const trimmedBaseUrl = baseUrl.replace(/\/$/, '');
  const path = region ? `/api/ais/latest?region=${encodeURIComponent(region)}` : '/api/ais/latest';
  const url = trimmedBaseUrl ? `${trimmedBaseUrl}${path}` : path;
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`AIS latest request failed: ${response.status}`);
  }
  return response.json();
}
