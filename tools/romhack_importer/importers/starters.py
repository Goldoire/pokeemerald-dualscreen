import re

def diff_starters(vanilla, source, consts, report):
    v_src = vanilla.read_file("src/starter_choose.c")
    s_src = source.read_file("src/starter_choose.c")
    
    if not v_src or not s_src:
        report.add_unsupported("system", "starter_choose.c", "Missing starter_choose.c")
        return []
        
    def extract_starter(src, index):
        # We look for something like `return SPECIES_TREECKO;` in `GetStarterPokemon`
        # Or look for `sStarterMon[3] = { SPECIES_TREECKO, SPECIES_TORCHIC, SPECIES_MUDKIP };`
        # Let's search for SPECIES_TREECKO etc. usually defined as `SPECIES_...` near starter logic.
        # Actually in pokeemerald, starters are in `src/starter_choose.c` as `sStarterMons = {SPECIES_TREECKO, SPECIES_TORCHIC, SPECIES_MUDKIP}`
        match = re.search(r'sStarterMon\[.*?\]\s*=\s*\{\s*([^,]+),\s*([^,]+),\s*([^,}]+),?', src, re.DOTALL)
        if match:
            return match.group(index + 1).strip()
        
        # Fallback to older pret format
        match = re.search(r'GetStarterPokemon.*?switch.*?case 0:.*?return ([A-Za-z0-9_]+);.*?case 1:.*?return ([A-Za-z0-9_]+);.*?case 2:.*?return ([A-Za-z0-9_]+);', src, re.DOTALL)
        if match:
            return match.group(index + 1).strip()
            
        return None

    starters = []
    for i in range(3):
        v_sp = extract_starter(v_src, i)
        s_sp = extract_starter(s_src, i)
        
        if v_sp and s_sp and v_sp != s_sp:
            starters.append({
                "slot": i,
                "species": consts.resolve(s_sp),
                "level": 5 # Hardcoded in vanilla, if they changed it, they'd have changed something else.
            })
            report.add_summary("starters", 1, 1, 0)
            
    return {"starters": starters} if starters else None
