import argparse
import sys
import os

from project import Project
from report import Report
from diff import compare_projects

def main():
    parser = argparse.ArgumentParser(description="Pokeemerald-Multiplatform ROM Hack Importer")
    parser.add_argument("--vanilla", required=True, help="Path to vanilla pret/pokeemerald decomp source tree")
    parser.add_argument("--source", required=True, help="Path to modified pokeemerald decomp hack source tree")
    parser.add_argument("--output", help="Path to generate the runtime mod package")
    parser.add_argument("--dry-run", action="store_true", help="Analyze and print report without generating output")
    parser.add_argument("--force", action="store_true", help="Overwrite existing output directory")
    
    args = parser.parse_args()
    
    # 1. Project Validation
    vanilla_proj = Project(args.vanilla)
    source_proj = Project(args.source)
    
    if not vanilla_proj.is_valid():
        print(f"[Importer][ERROR] Vanilla path '{args.vanilla}' does not appear to be a compatible pokeemerald project.")
        sys.exit(1)
        
    if not source_proj.is_valid():
        print(f"[Importer][ERROR] Source path '{args.source}' does not appear to be a compatible pokeemerald project.")
        sys.exit(1)
        
    # 2. Output Validation
    if not args.dry_run and args.output:
        if os.path.exists(args.output):
            if not args.force:
                print(f"[Importer][ERROR] Output directory '{args.output}' already exists. Use --force to overwrite.")
                sys.exit(1)
            # Remove only the exact output path if it's a directory
            import shutil
            if os.path.isdir(args.output):
                shutil.rmtree(args.output)
            else:
                os.remove(args.output)
                
    print("[Importer] Starting semantic comparison...")
    
    # 3. Diff and Parse
    report = Report()
    mod_data = compare_projects(vanilla_proj, source_proj, report, args.output)
    
    # 4. Write Output
    if not args.dry_run and args.output:
        import json
        os.makedirs(args.output, exist_ok=True)
        
        # Manifest
        mod_id = os.path.basename(os.path.normpath(args.output))
        manifest = {
            "format_version": 1,
            "id": mod_id,
            "name": mod_id.replace("_", " ").title(),
            "version": "imported",
            "priority": 100,
            "generated_by": "pokeemerald-multiplatform romhack_importer"
        }
        with open(os.path.join(args.output, "mod.json"), "w") as f:
            json.dump(manifest, f, indent=2)
            
        # Data Files
        data_dir = os.path.join(args.output, "data")
        os.makedirs(data_dir, exist_ok=True)
        for name, data in mod_data.items():
            if data: # Only emit files with content
                with open(os.path.join(data_dir, f"{name}.json"), "w") as f:
                    json.dump(data, f, indent=2)
                    
        # Write Report
        report.write_to_directory(args.output)
        print(f"[Importer] Mod package generated at {args.output}")
    else:
        print("[Importer] Dry run complete. Report summary:")
        print(report.generate_markdown())

if __name__ == "__main__":
    main()
