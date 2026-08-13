import re

def parse_moves_h(src):
    entries = {}
    
    block_pattern = re.compile(r'\[([A-Za-z0-9_]+)\]\s*=\s*\{(.*?)\},', re.DOTALL)
    
    for match in block_pattern.finditer(src):
        move_id = match.group(1)
        body = match.group(2)
        
        move = {}
        for line in body.splitlines():
            line = line.strip()
            if not line.startswith('.'): continue
            parts = line.split('=', 1)
            if len(parts) == 2:
                key = parts[0].strip().lstrip('.')
                val = parts[1].strip().rstrip(',')
                move[key] = val
                
        entries[move_id] = move
        
    return entries

def diff_moves(vanilla, source, consts, report):
    v_src = vanilla.read_file("src/data/battle_moves.h")
    s_src = source.read_file("src/data/battle_moves.h")
    
    if not v_src or not s_src:
        return []
        
    v_entries = parse_moves_h(v_src)
    s_entries = parse_moves_h(s_src)
    
    move_patches = []
    
    # Supported fields based on runtime capabilities (as per prompt)
    supported_fields = [
        "effect", "power", "type", "accuracy", "pp", 
        "secondaryEffectChance", "target", "priority", "flags"
    ]
    
    for m_id, s_data in s_entries.items():
        if m_id == 'MOVE_NONE': continue
        
        if m_id not in v_entries:
            report.add_unsupported("new_moves", m_id, "NEW MOVE - RUNTIME EXPANSION REQUIRED")
            continue
            
        v_data = v_entries[m_id]
        
        diffs = {}
        unsupported = []
        
        for key, val in s_data.items():
            if val != v_data.get(key):
                if key in supported_fields:
                    try:
                        diffs[key] = int(val)
                    except ValueError:
                        diffs[key] = consts.resolve(val)
                else:
                    unsupported.append(key)
                    
        if unsupported:
            report.add_unsupported("moves", m_id, f"Modified unsupported fields: {', '.join(unsupported)}")
            
        if diffs:
            diffs["id"] = consts.resolve(m_id)
            move_patches.append(diffs)
            report.add_summary("moves", 1, 1, 0)
            
    return {"moves": move_patches} if move_patches else None
