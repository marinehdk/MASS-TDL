export interface AisTarget {
  target_id: number;
  lat: number;
  lon: number;
  sog_kn: number;
  cog_deg: number;
  heading_deg?: number | null;
  source_sensor: 'ais';
}

export interface AisLatestResponse {
  provider: string;
  generated_at_utc: string;
  target_count: number;
  targets: AisTarget[];
}

export async function fetchLatestAisTargets(
  baseUrl = '',
): Promise<AisLatestResponse> {
  const url = baseUrl ? `${baseUrl.replace(/\/$/, '')}/api/ais/latest` : '/api/ais/latest';
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`AIS latest request failed: ${response.status}`);
  }
  return response.json();
}
