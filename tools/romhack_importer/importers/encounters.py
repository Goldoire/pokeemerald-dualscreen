import json
import os

def diff_encounters(vanilla, source, consts, report):
    v_path = vanilla.get_file("src/data/wild_encounters.json")
    s_path = source.get_file("src/data/wild_encounters.json")
    
    if not os.path.exists(v_path) or not os.path.exists(s_path):
        return []
        
    with open(v_path, "r") as f: v_json = json.load(f)
    with open(s_path, "r") as f: s_json = json.load(f)
    
    # We need map group/num mapping
    # Actually, wild_encounters.json in pokeemerald uses map names like "MAP_ROUTE_101".
    # Wait, the runtime ModManager expects "map_group" and "map_num" or just map string?
    # test_mod uses:
    # "maps": [ { "map_group": 0, "map_num": 16, "land": { "encounter_rate": 20, "slots": [...] } } ]
    # We need to map MAP_ROUTE_101 back to group and num using map_groups.json.
    
    groups_path = source.get_file("data/maps/map_groups.json")
    if not os.path.exists(groups_path):
        return []
    with open(groups_path, "r") as f:
        groups_data = json.load(f)
        
    map_to_idx = {}
    for group_idx, group_name in enumerate(groups_data.get("group_order", [])):
        maps = groups_data.get(group_name, [])
        for map_num, map_name in enumerate(maps):
            map_to_idx[f"MAP_{map_name.upper()}"] = (group_idx, map_num)
            # Route 101 is MAP_ROUTE_101, but the folder is Route101.
            # Wait, in wild_encounters.json, it's usually MAP_ROUTE_101. Let's handle variations.
            
    # Index vanilla by map
    v_maps = {}
    for entry in v_json.get("wild_encounter_groups", []):
        for e in entry.get("encounters", []):
            map_name = e.get("map")
            if map_name not in v_maps:
                v_maps[map_name] = {}
            # wild_encounters.json uses fields like "land_mons", "water_mons", "fishing_mons", "rock_smash_mons"
            for t in ["land_mons", "water_mons", "fishing_mons", "rock_smash_mons"]:
                if t in e:
                    v_maps[map_name][t] = e[t]
                    
    s_maps = {}
    for entry in s_json.get("wild_encounter_groups", []):
        for e in entry.get("encounters", []):
            map_name = e.get("map")
            if map_name not in s_maps:
                s_maps[map_name] = {}
            for t in ["land_mons", "water_mons", "fishing_mons", "rock_smash_mons"]:
                if t in e:
                    s_maps[map_name][t] = e[t]
                    
    encounter_patches = []
    
    # Map json keys to runtime keys
    type_mapping = {
        "land_mons": "land",
        "water_mons": "water",
        "fishing_mons": "fishing",
        "rock_smash_mons": "rock_smash"
    }
    
    for map_name, s_data in s_maps.items():
        v_data = v_maps.get(map_name, {})
        
        map_changed = False
        patch = {}
        
        if not map_name:
            continue
            
        if map_name in map_to_idx:
            group, num = map_to_idx[map_name]
            patch["map_group"] = group
            patch["map_num"] = num
        else:
            # Maybe the map name is slightly different. In pokeemerald, map names in map_groups.json
            # are like "Route101". MAP_ROUTE101 is usually the ID.
            # Let's try removing MAP_ and stripping underscores
            clean_name = map_name.replace("MAP_", "").replace("_", "")
            found = False
            for k, v in map_to_idx.items():
                if k.replace("MAP_", "").replace("_", "") == clean_name:
                    patch["map_group"] = v[0]
                    patch["map_num"] = v[1]
                    found = True
                    break
            if not found:
                report.add_unsupported("encounters", map_name, "Cannot resolve map ID")
                continue
                
        for t_src, t_run in type_mapping.items():
            s_type_data = s_data.get(t_src)
            v_type_data = v_data.get(t_src)
            
            if s_type_data != v_type_data and s_type_data:
                map_changed = True
                
                patch[t_run] = {
                    "encounter_rate": s_type_data.get("encounter_rate", 0),
                    "slots": []
                }
                
                for mon in s_type_data.get("mons", []):
                    patch[t_run]["slots"].append({
                        "species": consts.resolve(mon.get("species", 0)),
                        "min_level": mon.get("min_level", 0),
                        "max_level": mon.get("max_level", 0)
                    })
                    
        if map_changed:
            encounter_patches.append(patch)
            report.add_summary("encounters", 1, 1, 0)
            
    return {"maps": encounter_patches} if encounter_patches else None
