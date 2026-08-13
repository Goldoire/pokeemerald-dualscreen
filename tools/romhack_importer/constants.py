import os
import re

class Constants:
    def __init__(self, proj):
        self.proj = proj
        self.mappings = {}
        self._parse_headers()
        
    def _parse_headers(self):
        # Extremely simplified parser for V1
        headers = [
            "include/constants/event_objects.h",
            "include/constants/event_object_movement.h",
            "include/constants/species.h",
            "include/constants/opponents.h",
            "include/constants/moves.h",
            "include/constants/items.h"
        ]
        
        for h in headers:
            path = self.proj.get_file(h)
            if not os.path.exists(path):
                continue
            with open(path, "r") as f:
                for line in f:
                    match = re.match(r'#define\s+([A-Za-z0-9_]+)\s+([0-9a-fA-Fx]+)', line)
                    if match:
                        name = match.group(1)
                        val = match.group(2)
                        try:
                            if val.startswith("0x") or val.startswith("0X"):
                                self.mappings[name] = int(val, 16)
                            else:
                                self.mappings[name] = int(val)
                        except ValueError:
                            pass
                            
    def resolve(self, value):
        if isinstance(value, str):
            if value in self.mappings:
                return self.mappings[value]
            try:
                if value.startswith("0x") or value.startswith("0X"):
                    return int(value, 16)
                return int(value)
            except ValueError:
                return 0
        return value
