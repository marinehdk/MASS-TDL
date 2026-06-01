import os
import subprocess
import pytest


def _get_container_name() -> str:
    try:
        res = subprocess.run(
            ["docker", "ps", "--filter", "name=sil-nodes", "--format", "{{.Names}}"],
            capture_output=True, text=True, check=True
        )
        names = res.stdout.strip().splitlines()
        if names and names[0]:
            return names[0]
    except Exception:
        pass
    return "mass-l3-sil-sil-nodes-1"


@pytest.mark.integration
def test_sb3_smoke():
    container = _get_container_name()
    
    # Locate run_sb3_smoke.py relative to this file
    current_dir = os.path.dirname(os.path.abspath(__file__))
    local_script_path = os.path.join(current_dir, "run_sb3_smoke.py")
    
    # Copy run_sb3_smoke.py to the container
    cp_cmd = ["docker", "cp", local_script_path, f"{container}:/tmp/run_sb3_smoke.py"]
    print(f"Copying script to container: {' '.join(cp_cmd)}")
    subprocess.run(cp_cmd, check=True)
    
    try:
        # Execute the SB3 smoke test inside the container
        exec_cmd = [
            "docker", "exec", "-t", container,
            "bash", "-c",
            "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && python3 /tmp/run_sb3_smoke.py"
        ]
        print(f"Executing SB3 PPO smoke test in container: {' '.join(exec_cmd)}")
        
        res = subprocess.run(exec_cmd, capture_output=True, text=True)
        print("--- STDOUT ---")
        print(res.stdout)
        print("--- STDERR ---")
        print(res.stderr)
        
        assert res.returncode == 0, f"SB3 PPO smoke test failed with exit code {res.returncode}"
    finally:
        # Clean up temporary test runner inside container
        subprocess.run(["docker", "exec", container, "rm", "-f", "/tmp/run_sb3_smoke.py"], capture_output=True)
