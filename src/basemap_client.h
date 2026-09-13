#pragma once
// Fetches OpenStreetMap raster tiles (the same tile.openstreetmap.org layer the
// device's own web config page already uses via Leaflet) and stitches them into
// basemap.h's back buffer, centered on (lat,lon) at a zoom chosen so the canvas
// edge roughly matches rangeKm — i.e. it lines up with the radar scope's rings.
// Call this rarely (home moved / range changed), never on the poll cadence: the
// map for a fixed home position never goes stale the way weather imagery does.
bool basemap_fetch(double lat, double lon, float rangeKm);
