import os

class Project:
    def __init__(self, path):
        self.path = path
        
    def is_valid(self):
        """Check for core pokeemerald structures."""
        required = [
            "include/constants",
            "src",
            "data",
            "Makefile"
        ]
        return all(os.path.exists(os.path.join(self.path, p)) for p in required)
        
    def get_file(self, rel_path):
        """Get absolute path to a file in the project."""
        return os.path.join(self.path, rel_path)
        
    def read_file(self, rel_path):
        """Read content of a file, return empty string if not found."""
        try:
            with open(self.get_file(rel_path), "r", encoding="utf-8") as f:
                return f.read()
        except Exception:
            return ""
