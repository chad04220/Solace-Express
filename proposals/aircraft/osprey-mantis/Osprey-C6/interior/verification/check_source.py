#!/usr/bin/env python3
from pathlib import Path
import hashlib,json,re
r=Path(__file__).resolve().parents[1];m=(r/'mapOspreyCabinTrim.glsl').read_text()
checks={
 'module_entry_point': 'vec2 mapOspreyCabinTrim(vec3 p)' in m,
 'static_no_control_or_time_reads':not re.search(r'\b(gCtl|uCtl|uTime|gPS|uPS)\b',m),
 'actual_fuselage_clip': 'r.x=max(r.x,sdFuselage(p)+.090)' in m,
 'unique_id_range':all(120<=int(x)<=125 for x in re.findall(r',\s*(1[12]\d)\.0\)',m)),
 'four_passenger_seats':'row<2' in m and 'abs(p.x)-.325' in m,
 'material_albedo_defined':'vec3 ospreyCabinAlbedo(int id)' in m,
 'no_aircraft_row_fields':not re.search(r'\b(?:struct|uniform)\s+(?:AircraftSpec|ModelDef)\b',m),
}
result={'checks':checks,'module_sha256':hashlib.sha256(m.encode()).hexdigest(),'passed':all(checks.values())}
print(json.dumps(result,indent=2));assert result['passed']
