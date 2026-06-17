Import("env")

import subprocess
import sys
from pathlib import Path

scripts_dir = Path(env["PROJECT_DIR"]) / "scripts"
for name in ("embed_wii_i2c.py", "embed_textures.py"):
    subprocess.run([sys.executable, str(scripts_dir / name)], check=True)
