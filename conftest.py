import sys
from pathlib import Path

# Ensure src/ is on the Python path so tests can import modules directly
sys.path.insert(0, str(Path(__file__).parent / "src"))
