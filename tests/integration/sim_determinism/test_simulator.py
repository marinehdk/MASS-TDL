import os
import subprocess
import pytest

def _get_container_name() -> str:
    return "mass-l3-sil-sil-nodes-1"

@pytest.mark.integration
def test_simulator():
    container = _get_container_name()
    
    # Locate run_simulator_test.py relative to this file
    current_dir = os.path.dirname(os.path.abspath(__file__))
    local_script_path = os.path.join(current_dir, "run_simulator_test.py")
    
    # Copy run_simulator_test.py to the container
    cp_cmd = ["docker", "cp", local_script_path, f"{container}:/tmp/run_simulator_test.py"]
    print(f"Copying script to container: {' '.join(cp_cmd)}")
    subprocess.run(cp_cmd, check=True)
    
    # Execute the simulator integration test inside the container
    exec_cmd = [
        "docker", "exec", "-t", container,
        "bash", "-c",
        "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && python3 /tmp/run_simulator_test.py"
    ]
    print(f"Executing simulator integration test in container: {' '.join(exec_cmd)}")
    
    res = subprocess.run(exec_cmd, capture_output=True, text=True)
    print("--- STDOUT ---")
    print(res.stdout)
    print("--- STDERR ---")
    print(res.stderr)
    
    assert res.returncode == 0, f"Simulator integration test failed with exit code {res.returncode}"
