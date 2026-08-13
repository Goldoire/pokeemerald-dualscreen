import re

def parse_species_h(src):
    entries = {}
    current_species = None
    
    lines = src.splitlines()
    for i, line in enumerate(lines):
        line = line.strip()
        if line.startswith("[SPECIES_"):
            import re
            match = re.search(r"\[([A-Za-z0-9_]+)\]", line)
            if match:
                current_species = match.group(1)
                entries[current_species] = {}
        elif current_species and line.startswith(".") and "=" in line:
            parts = line.split("=", 1)
            key = parts[0].strip().lstrip(".")
            val = parts[1].strip().rstrip(",")
            entries[current_species][key] = val
            
    return entries

def diff_species(vanilla, source, consts, report):
    v_src = vanilla.read_file("src/data/pokemon/species_info.h")
    s_src = source.read_file("src/data/pokemon/species_info.h")
    
    if not v_src or not s_src:
        return []
        
    v_entries = parse_species_h(v_src)
    s_entries = parse_species_h(s_src)
    
    species_patches = []
    
    for sp_name, s_data in s_entries.items():
        if sp_name not in v_entries:
            report.add_unsupported("new_species", sp_name, "Species does not exist in vanilla")
            continue
            
        v_data = v_entries[sp_name]
        diffs = {}
        
        # Check standard fields
        fields = [
            ("baseHP", "base_hp"),
            ("baseAttack", "base_attack"),
            ("baseDefense", "base_defense"),
            ("baseSpeed", "base_speed"),
            ("baseSpAttack", "base_sp_attack"),
            ("baseSpDefense", "base_sp_defense")
        ]
        
        for c_key, j_key in fields:
            if s_data.get(c_key) != v_data.get(c_key) and s_data.get(c_key):
                val = s_data.get(c_key)
                try:
                    diffs[j_key] = int(val)
                except ValueError:
                    diffs[j_key] = consts.resolve(val)
                    
        if diffs:
            diffs["id"] = consts.resolve(sp_name)
            species_patches.append(diffs)
            report.add_summary("species", 1, 1, 0)
            
    return {"species": species_patches} if species_patches else None
