Import("env")

import subprocess
import sys
from pathlib import Path

script = Path(env["PROJECT_DIR"]) / "scripts" / "embed_textures.py"
subprocess.run([sys.executable, str(script)], check=True)
