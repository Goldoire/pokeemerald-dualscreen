import os
import filecmp
import shutil
import subprocess

def diff_graphics(vanilla, source, consts, report, out_dir):
    v_dir = vanilla.get_file("graphics/trainers/front_pics")
    s_dir = source.get_file("graphics/trainers/front_pics")
    
    if not os.path.exists(v_dir) or not os.path.exists(s_dir):
        return
        
    gbagfx_path = source.get_file("tools/gbagfx/gbagfx")
    if not os.path.exists(gbagfx_path):
        # Fallback to vanilla gbagfx if source doesn't have it built? Or just report error.
        gbagfx_path = vanilla.get_file("tools/gbagfx/gbagfx")
        
    if not os.path.exists(gbagfx_path):
        report.add_unsupported("system", "gbagfx", "gbagfx tool not built. Run make tools in project.")
        return
        
    # Mapping trainer names to IDs. We need to parse src/data/graphics/trainers.h to map
    # front pics to their IDs. But trainer IDs are just TRAINER_PIC_XXX.
    # We can parse include/constants/trainers.h? No, opponents.h has TRAINER_PIC_XXX.
    # Actually, the prompt says `<ID>.4bpp` where ID is the numeric ID.
    # Let's parse src/data/graphics/trainers.h to find which pic goes with which ID.
    # It has `[TRAINER_PIC_HIKER] = { .data = gTrainerFrontPic_Hiker, .size = ... }`
    # Let's skip that complexity for V1 and just map from file name if possible, or 
    # parse src/data/graphics/trainers.h.
    
    trainers_h = source.read_file("src/data/graphics/trainers.h")
    pic_to_id = {}
    if trainers_h:
        import re
        # [TRAINER_PIC_HIKER] = { .size = 0x800, .data = gTrainerFrontPic_Hiker }
        for match in re.finditer(r'\[([A-Za-z0-9_]+)\]\s*=\s*\{[^}]*\.data\s*=\s*gTrainerFrontPic_([A-Za-z0-9_]+)', trainers_h):
            pic_id_str = match.group(1)
            pic_name = match.group(2).lower()
            pic_to_id[pic_name] = consts.resolve(pic_id_str)
            
    # Now check for modified pngs
    for file in os.listdir(s_dir):
        if not file.endswith(".png"):
            continue
            
        s_path = os.path.join(s_dir, file)
        v_path = os.path.join(v_dir, file)
        
        if not os.path.exists(v_path) or not filecmp.cmp(v_path, s_path, shallow=False):
            # It changed! We need to convert it.
            pic_name_base = file[:-4].lower() # e.g. "hiker"
            
            # Try to resolve ID
            numeric_id = pic_to_id.get(pic_name_base)
            if numeric_id is None:
                # Some files have different naming conventions
                continue
                
            out_pics_dir = os.path.join(out_dir, "graphics", "trainers", "front_pics")
            os.makedirs(out_pics_dir, exist_ok=True)
            
            out_4bpp = os.path.join(out_pics_dir, f"{numeric_id}.4bpp")
            
            # Call gbagfx
            try:
                subprocess.run([gbagfx_path, s_path, out_4bpp], check=True, stdout=subprocess.DEVNULL)
                report.add_summary("graphics", 1, 1, 0)
            except subprocess.CalledProcessError:
                report.add_unsupported("graphics", pic_name_base, "gbagfx conversion failed")
