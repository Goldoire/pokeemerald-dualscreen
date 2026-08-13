import os
import re

def parse_script_inc(proj, map_name):
    script_path = proj.get_file(f"data/maps/{map_name}/scripts.inc")
    if not os.path.exists(script_path):
        return {}, {}
    
    with open(script_path, "r") as f:
        content = f.read()
        
    scripts = {}
    strings = {}
    current_label = None
    
    for line in content.splitlines():
        orig_line = line
        line = line.split("@")[0].strip()
        if not line:
            continue
            
        if line.endswith("::") or line.endswith(":"):
            current_label = line.rstrip(":")
            scripts[current_label] = []
        elif current_label:
            if line.startswith(".string"):
                match = re.search(r'\.string\s+"([^"]*)"', orig_line)
                if match:
                    strings[current_label] = match.group(1).replace("$", "")
            else:
                parts = re.split(r"\s+", line, maxsplit=1)
                cmd = parts[0]
                args = parts[1] if len(parts) > 1 else ""
                scripts[current_label].append({"cmd": cmd, "args": args})
                
    return scripts, strings

def compile_script(script_cmds, strings, report, label):
    compiled = []
    supported = ["end", "return", "lock", "faceplayer", "release", "lockall", "releaseall", "msgbox"]
    for scmd in script_cmds:
        cmd = scmd["cmd"]
        args = scmd["args"]
        
        if cmd == "msgbox":
            parts = [p.strip() for p in args.split(",")]
            text_label = parts[0]
            text = strings.get(text_label, f"[{text_label}]")
            compiled.append({"cmd": "msgbox", "text": text})
        elif cmd in supported:
            compiled.append({"cmd": cmd})
        else:
            report.add_unsupported("scripts", label, f"Unsupported command: {cmd}")
            return None
            
    return compiled
