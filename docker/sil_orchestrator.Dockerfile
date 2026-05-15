# SIL Orchestrator — FastAPI REST plane.
# Spec: docs/Design/SIL/2026-05-12-sil-architecture-design.md §1 orchestration plane
FROM mass-l3/ci:humble-ubuntu22.04

WORKDIR /opt/sil

# Install python dependencies for FastAPI
RUN apt-get update && apt-get install -y python3-pip && \
    pip3 install --no-cache-dir \
    fastapi==0.115.4 \
    uvicorn[standard]==0.32.0 \
    websockets==12.0 \
    pydantic==2.9.2 \
    protobuf==5.28.2 \
    pyyaml==6.0.2

# Copy orchestrator package
COPY src/sil_orchestrator /opt/sil/sil_orchestrator

# Copy and build sil_msgs so rclpy can use it
COPY src/sim_workbench/sil_msgs /opt/sil/src/sil_msgs
RUN /bin/bash -c "source /opt/ros/humble/setup.bash && colcon build --base-paths /opt/sil/src"

# Persistent directories
RUN mkdir -p /var/sil/scenarios /var/sil/runs /var/sil/exports

ENV PYTHONPATH=/opt/sil

EXPOSE 8000

CMD ["/bin/bash", "-c", "source /opt/ros/humble/setup.bash && source /opt/sil/install/setup.bash && python3 -m uvicorn sil_orchestrator.main:app --host 0.0.0.0 --port 8000"]
