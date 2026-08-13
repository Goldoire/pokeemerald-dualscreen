import json
import os

class Report:
    def __init__(self):
        self.summary = {
            "maps": {"changed": 0, "imported": 0, "unsupported": 0},
            "scripts": {"changed": 0, "imported": 0, "unsupported": 0},
        }
        self.unsupported = {
            "new_maps": [],
            "resized_maps": [],
            "custom_code": [],
            "scripts": []
        }
        
    def add_summary(self, category, changed, imported, unsupported=0):
        if category not in self.summary:
            self.summary[category] = {"changed": 0, "imported": 0, "unsupported": 0}
        self.summary[category]["changed"] += changed
        self.summary[category]["imported"] += imported
        self.summary[category]["unsupported"] += unsupported
        
    def add_unsupported(self, category, name, reason):
        if category not in self.unsupported:
            self.unsupported[category] = []
        self.unsupported[category].append({"name": name, "reason": reason})

    def generate_markdown(self):
        lines = ["# Import Report", ""]
        
        lines.append("## Summary")
        for k, v in self.summary.items():
            lines.append(f"{k.capitalize()}:")
            lines.append(f"{v['changed']} changed")
            lines.append(f"{v['imported']} imported")
            if v['unsupported'] > 0:
                lines.append(f"{v['unsupported']} unsupported")
            lines.append("")
            
        lines.append("## Unsupported Content")
        for k, v in self.unsupported.items():
            if v:
                lines.append(f"### {k.replace('_', ' ').capitalize()}")
                for item in v:
                    lines.append(f"- **{item['name']}**")
                    if item['reason']:
                        lines.append(f"  Reason: {item['reason']}")
                lines.append("")
                
        return "\n".join(lines)

    def write_to_directory(self, output_dir):
        with open(os.path.join(output_dir, "PORT_REPORT.md"), "w") as f:
            f.write(self.generate_markdown())
            
        with open(os.path.join(output_dir, "port_report.json"), "w") as f:
            json.dump({
                "summary": self.summary,
                "unsupported": self.unsupported
            }, f, indent=2)
