import re

def parse_items_h(src):
    entries = {}
    
    block_pattern = re.compile(r'\[([A-Za-z0-9_]+)\]\s*=\s*\{(.*?)\},', re.DOTALL)
    
    for match in block_pattern.finditer(src):
        item_id = match.group(1)
        body = match.group(2)
        
        item = {}
        for line in body.splitlines():
            line = line.strip()
            if not line.startswith('.'): continue
            parts = line.split('=', 1)
            if len(parts) == 2:
                key = parts[0].strip().lstrip('.')
                val = parts[1].strip().rstrip(',')
                item[key] = val
                
        entries[item_id] = item
        
    return entries

def diff_items(vanilla, source, consts, report):
    v_src = vanilla.read_file("src/data/items.h")
    s_src = source.read_file("src/data/items.h")
    
    if not v_src or not s_src:
        return []
        
    v_entries = parse_items_h(v_src)
    s_entries = parse_items_h(s_src)
    
    item_patches = []
    
    # Supported fields based on runtime capabilities (as per prompt)
    supported_fields = [
        "price", "pocket", "importance", "holdEffect", "holdEffectParam"
    ]
    
    pointer_fields = [
        "fieldUseFunc", "battleUseFunc"
    ]
    
    for i_id, s_data in s_entries.items():
        if i_id == 'ITEM_NONE': continue
        
        if i_id not in v_entries:
            report.add_unsupported("new_items", i_id, "NEW ITEM - RUNTIME EXPANSION REQUIRED")
            continue
            
        v_data = v_entries[i_id]
        
        diffs = {}
        unsupported = []
        has_custom_code = False
        
        for key, val in s_data.items():
            if val != v_data.get(key):
                if key in pointer_fields:
                    has_custom_code = True
                elif key in supported_fields:
                    try:
                        diffs[key] = int(val)
                    except ValueError:
                        diffs[key] = consts.resolve(val)
                else:
                    unsupported.append(key)
                    
        if has_custom_code:
            report.add_unsupported("custom_code", f"items.h ({i_id})", "CUSTOM ITEM CODE - MANUAL PORT REQUIRED")
            
        if unsupported:
            report.add_unsupported("items", i_id, f"Modified unsupported fields: {', '.join(unsupported)}")
            
        if diffs and not has_custom_code:
            resolved_id = consts.resolve(i_id)
            if resolved_id == 0 and i_id != "ITEM_NONE":
                report.add_unsupported("items", i_id, "UNRESOLVED ITEM ID")
            else:
                diffs["id"] = resolved_id
                item_patches.append(diffs)
                report.add_summary("items", 1, 1, 0)
            
    return {"items": item_patches} if item_patches else None
