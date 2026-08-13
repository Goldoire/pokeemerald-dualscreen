import re
import os

def parse_species_names(src):
    entries = {}
    for line in src.splitlines():
        match = re.search(r'\[([A-Za-z0-9_]+)\]\s*=\s*_\("([^"]+)"\)', line)
        if match:
            entries[match.group(1)] = match.group(2)
    return entries

def diff_text(vanilla, source, consts, report):
    v_src = vanilla.read_file("src/data/text/species_names.h")
    s_src = source.read_file("src/data/text/species_names.h")
    
    if not v_src or not s_src:
        return []
        
    v_entries = parse_species_names(v_src)
    s_entries = parse_species_names(s_src)
    
    species_names_patches = []
    
    for s_id, s_name in s_entries.items():
        if s_id == 'SPECIES_NONE': continue
        
        if s_id not in v_entries:
            # Already handled by species importer
            continue
            
        if s_name != v_entries[s_id]:
            species_names_patches.append({
                "id": consts.resolve(s_id),
                "name": s_name
            })
            report.add_summary("text", 1, 1, 0)
            
    return {"species_names": species_names_patches} if species_names_patches else None
