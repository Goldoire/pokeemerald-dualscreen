import re

def parse_trainer_parties(src, consts):
    parties = {}
    
    # Match blocks like: static const struct XXX sParty_Name[] = { { ... }, { ... } };
    block_pattern = re.compile(r'static\s+const\s+struct\s+([A-Za-z0-9_]+)\s+([A-Za-z0-9_]+)\[\]\s*=\s*\{(.*?)\};', re.DOTALL)
    
    for match in block_pattern.finditer(src):
        struct_type = match.group(1)
        party_name = match.group(2)
        body = match.group(3)
        
        mons = []
        # Match individual pokemon blocks { ... }
        mon_blocks = re.findall(r'\{([^{}]+)\}', body)
        for mon_body in mon_blocks:
            mon = {}
            for line in mon_body.splitlines():
                if '=' not in line:
                    continue
                if ':' in line: continue
                line = line.strip()
                if not line.startswith('.'): continue
                parts = line.split('=', 1)
                if len(parts) == 2:
                    key = parts[0].strip().lstrip('.')
                    val = parts[1].strip().rstrip(',')
                    
                    if key in ['lvl', 'species']:
                        try:
                            mon[key] = int(val)
                        except ValueError:
                            mon[key] = consts.resolve(val)
                    else:
                        mon[key] = val # Store it so we can report unsupported fields
            mons.append(mon)
        parties[party_name] = mons
    return parties

def parse_trainers(src):
    trainers = {}
    
# Split by [TRAINER_
    parts = src.split("[TRAINER_")
    for part in parts[1:]:
        t_id = "TRAINER_" + part.split("]", 1)[0]
        
        trainer = {}
        for line in part.splitlines():
            line = line.strip()
            if not line.startswith("."): continue
            kv = line.split("=", 1)
            if len(kv) == 2:
                key = kv[0].strip().lstrip(".")
                val = kv[1].strip().rstrip(",")
                if key == "party":
                    import re
                    party_match = re.search(r"\(([A-Za-z0-9_]+)\)", val)
                    if party_match:
                        trainer["party_ref"] = party_match.group(1)
                else:
                    trainer[key] = val
        trainers[t_id] = trainer
    return trainers

def diff_trainers(vanilla, source, consts, report):
    v_parties_src = vanilla.read_file("src/data/trainer_parties.h")
    s_parties_src = source.read_file("src/data/trainer_parties.h")
    v_trainers_src = vanilla.read_file("src/data/trainers.h")
    s_trainers_src = source.read_file("src/data/trainers.h")
    
    if not s_parties_src or not s_trainers_src:
        return []
        
    v_parties = parse_trainer_parties(v_parties_src, consts) if v_parties_src else {}
    s_parties = parse_trainer_parties(s_parties_src, consts)
    
    v_trainers = parse_trainers(v_trainers_src) if v_trainers_src else {}
    s_trainers = parse_trainers(s_trainers_src)
    
    trainer_patches = []
    
    for t_id, s_data in s_trainers.items():
        if t_id == 'TRAINER_NONE': continue
        
        if t_id not in v_trainers:
            report.add_unsupported("new_trainers", t_id, "New trainer IDs not supported by runtime")
            continue
            
        v_data = v_trainers[t_id]
        
        # Check unsupported field changes in trainers.h
        unsupported_fields = []
        for key in s_data:
            if key != 'party_ref' and s_data[key] != v_data.get(key):
                unsupported_fields.append(key)
                
        if unsupported_fields:
            report.add_unsupported("trainers", t_id, f"Modified unsupported fields: {', '.join(unsupported_fields)}")
            
        # Check party changes
        s_party_ref = s_data.get('party_ref')
        v_party_ref = v_data.get('party_ref')
        
        s_party = s_parties.get(s_party_ref, []) if s_party_ref else []
        v_party = v_parties.get(v_party_ref, []) if v_party_ref else []
        
        party_changed = False
        if len(s_party) != len(v_party):
            party_changed = True
        else:
            for i, s_mon in enumerate(s_party):
                v_mon = v_party[i]
                if s_mon.get('species') != v_mon.get('species') or s_mon.get('lvl') != v_mon.get('lvl'):
                    party_changed = True
                
                # Check unsupported party fields (items, moves, etc.)
                unsupported_mon = []
                for m_key, m_val in s_mon.items():
                    if m_key not in ['species', 'lvl', 'iv'] and m_val != v_mon.get(m_key):
                        unsupported_mon.append(m_key)
                if unsupported_mon:
                    report.add_unsupported("trainers", f"{t_id} (Mon {i})", f"Modified unsupported party fields: {', '.join(unsupported_mon)}")
                    
        if party_changed:
            patch = {
                "id": consts.resolve(t_id),
                "party": [{"species": m.get("species", 0), "level": m.get("lvl", 0)} for m in s_party]
            }
            trainer_patches.append(patch)
            report.add_summary("trainers", 1, 1, 0)
            
    return {"trainers": trainer_patches} if trainer_patches else None
