import os
import subprocess
import pytest

def _get_container_name() -> str:
    return "mass-l3-sil-sil-nodes-1"

@pytest.mark.integration
def test_real_stack_batch():
    container = _get_container_name()
    
    # Ensure pydantic is installed in the container
    print("Installing pydantic in container...")
    subprocess.run(["docker", "exec", container, "pip", "install", "pydantic"], check=True)
    
    # Locate run_real_stack_batch.py relative to this file
    current_dir = os.path.dirname(os.path.abspath(__file__))
    local_script_path = os.path.join(current_dir, "run_real_stack_batch.py")
    
    # Copy tools to container /tmp/
    subprocess.run(["docker", "exec", container, "rm", "-rf", "/tmp/tools"], capture_output=True)
    subprocess.run(["docker", "cp", "tools", f"{container}:/tmp/"], check=True)
    
    # Copy run_real_stack_batch.py to the container
    cp_cmd = ["docker", "cp", local_script_path, f"{container}:/tmp/run_real_stack_batch.py"]
    print(f"Copying script to container: {' '.join(cp_cmd)}")
    subprocess.run(cp_cmd, check=True)
    
    try:
        # Execute the simulator integration test inside the container
        exec_cmd = [
            "docker", "exec", "-t", container,
            "bash", "-c",
            "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && python3 /tmp/run_real_stack_batch.py"
        ]
        print(f"Executing real stack batch integration test in container: {' '.join(exec_cmd)}")
        
        res = subprocess.run(exec_cmd, capture_output=True, text=True)
        print("--- STDOUT ---")
        print(res.stdout)
        print("--- STDERR ---")
        print(res.stderr)
        
        assert res.returncode == 0, f"Real stack batch integration test failed with exit code {res.returncode}"
    finally:
        # Clean up temporary test runner and copied tools inside container
        subprocess.run(["docker", "exec", container, "rm", "-rf", "/tmp/run_real_stack_batch.py", "/tmp/tools"], capture_output=True)
