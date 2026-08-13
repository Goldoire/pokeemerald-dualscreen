import json
import os
import struct

def parse_map_groups(proj):
    groups_file = proj.get_file("data/maps/map_groups.json")
    if not os.path.exists(groups_file):
        return None
    with open(groups_file, "r") as f:
        return json.load(f)

def load_map_bin(proj, layout_name):
    layouts_file = proj.get_file("data/layouts/layouts.json")
    if not os.path.exists(layouts_file):
        return None, None, None
    with open(layouts_file, "r") as f:
        layouts = json.load(f)
    
    layout_info = next((l for l in layouts.get("layouts", []) if l["id"] == layout_name), None)
    if not layout_info:
        return None, None, None
        
    width = layout_info["width"]
    height = layout_info["height"]
    bin_path = proj.get_file(f"data/layouts/{layout_info['blockdata_filepath']}")
    if not os.path.exists(bin_path):
        return None, None, None
        
    with open(bin_path, "rb") as f:
        data = f.read()
        
    tiles = struct.unpack(f"<{width*height}H", data)
    return tiles, width, height


from importers.scripts import parse_script_inc, compile_script

def extract_scripts_from_events(map_group, map_num, map_name, vanilla_events, source_events, vanilla_proj, source_proj, consts, report):
    scripts_out = []
    
    v_objs = vanilla_events.get("object_events", [])
    s_objs = source_events.get("object_events", [])
    
    v_scripts, v_strings = parse_script_inc(vanilla_proj, map_name)
    s_scripts, s_strings = parse_script_inc(source_proj, map_name)
    
    changed_objs = []
    
    for i, s_obj in enumerate(s_objs):
        if i < len(v_objs):
            v_obj = v_objs[i]
            
            s_script_label = s_obj.get("script")
            v_script_label = v_obj.get("script")
            
            s_script_cmds = s_scripts.get(s_script_label)
            v_script_cmds = v_scripts.get(v_script_label)
            
            s_script_str = str(s_script_cmds) + str(s_strings) if s_script_cmds else ""
            v_script_str = str(v_script_cmds) + str(v_strings) if v_script_cmds else ""
            
            if s_script_label != v_script_label or s_script_str != v_script_str:
                if s_script_label in s_scripts:
                    compiled = compile_script(s_scripts[s_script_label], s_strings, report, f"{map_name}.{s_script_label}")
                    if compiled:
                        scripts_out.append({
                            "target": {
                                "map_group": map_group,
                                "map_num": map_num,
                                "object_index": i + 1  # Note: 1-indexed for local_id
                            },
                            "commands": compiled
                        })
                        report.add_summary("scripts", 1, 1, 0)
                else:
                    if s_script_label and s_script_label != "0x0":
                        report.add_unsupported("scripts", f"{map_name}.{s_script_label}", "Script not found in scripts.inc or unsupported format")
                        
            if s_obj != v_obj:
                changed_objs.append({
                    "index": i,
                    "x": s_obj.get("x"),
                    "y": s_obj.get("y"),
                    "graphics_id": consts.resolve(s_obj.get("graphics_id")),
                    "movement_type": consts.resolve(s_obj.get("movement_type"))
                    
                })
        else:
            report.add_unsupported("object_events", f"Map {map_group}.{map_num} Object {i}", "New objects unsupported")
            
    return changed_objs, scripts_out

    scripts = []
    
    # Object Events
    v_objs = vanilla_events.get("object_events", [])
    s_objs = source_events.get("object_events", [])
    
    # We compare scripts by checking if source changed the script name
    # Wait, the runtime supports parsing script .inc files! We need the script bytecode!
    # But for Version 1, the prompt says:
    # "Convert source labels into runtime-local labels. Do not expose addresses."
    # Since writing a full script compiler/importer is massive, I'll mock a simple script detection.
    # Actually, the user asked for:
    # "The importer should translate compatible source-script syntax into that runtime representation."
    # I will defer script parsing to `importers/scripts.py`.
    
    # For now, just object events.
    changed_objs = []
    
    for i, s_obj in enumerate(s_objs):
        if i < len(v_objs):
            v_obj = v_objs[i]
            if s_obj != v_obj:
                changed_objs.append({
                    "index": i,
                    "x": s_obj.get("x"),
                    "y": s_obj.get("y"),
                    "graphics_id": consts.resolve(s_obj.get("graphics_id")),
                    "movement_type": consts.resolve(s_obj.get("movement_type"))
                    
                })
        else:
            report.add_unsupported("object_events", f"Map {map_group}.{map_num} Object {i}", "New objects unsupported")
            
    return changed_objs, [] # return empty scripts for now

def diff_maps(vanilla, source, consts, report):
    v_groups = parse_map_groups(vanilla)
    s_groups = parse_map_groups(source)
    
    if not v_groups or not s_groups:
        report.add_unsupported("system", "map_groups.json", "Missing map_groups.json in one of the projects")
        return [], []
        
    map_patches = []
    script_patches = []
    
    for group_idx, group_name in enumerate(v_groups.get("group_order", [])):
        v_maps = v_groups.get(group_name, [])
        s_maps = s_groups.get(group_name, [])
        
        for map_num, map_name in enumerate(v_maps):
            if map_num >= len(s_maps) or s_maps[map_num] != map_name:
                report.add_unsupported("maps", map_name, "Map reorganization unsupported")
                continue
                
            v_map_json = vanilla.get_file(f"data/maps/{map_name}/map.json")
            s_map_json = source.get_file(f"data/maps/{map_name}/map.json")
            
            if not os.path.exists(v_map_json) or not os.path.exists(s_map_json):
                continue
                
            with open(v_map_json, "r") as f: v_json = json.load(f)
            with open(s_map_json, "r") as f: s_json = json.load(f)
            
            # 1. Tile changes
            v_tiles, v_w, v_h = load_map_bin(vanilla, v_json["layout"])
            s_tiles, s_w, s_h = load_map_bin(source, s_json["layout"])
            
            tile_diffs = []
            if v_tiles and s_tiles:
                if v_w != s_w or v_h != s_h:
                    report.add_unsupported("resized_maps", map_name, f"Vanilla: {v_w}x{v_h}, Hack: {s_w}x{s_h}")
                    continue # Skip the entire map to avoid misleading partial port
                else:
                    for i in range(v_w * v_h):
                        if v_tiles[i] != s_tiles[i]:
                            x = i % v_w
                            y = i // v_w
                            cell = s_tiles[i]
                            tile_diffs.append({
                                "x": x,
                                "y": y,
                                "metatile": cell & 0x3FF,
                                "collision": (cell >> 10) & 3,
                                "elevation": (cell >> 12) & 15
                            })
                            
            # 2. Event changes
            changed_objs, scripts = extract_scripts_from_events(group_idx, map_num, map_name, v_json, s_json, vanilla, source, consts, report)
            script_patches.extend(scripts)
            
            if tile_diffs or changed_objs:
                patch = {
                    "map_group": group_idx,
                    "map_num": map_num
                }
                if tile_diffs:
                    patch["tiles"] = tile_diffs
                if changed_objs:
                    patch["objects"] = changed_objs
                    
                map_patches.append(patch)
                report.add_summary("maps", 1, 1, 0)
                
    return ({"maps": map_patches} if map_patches else None, {"scripts": script_patches} if script_patches else None)
