import sys
from pathlib import Path

# Ensure src/ is on the Python path so tests can import modules directly
sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

# Create test-results/ before any test or plugin writes output there
Path(__file__).parent.parent.joinpath("test-results").mkdir(exist_ok=True)
