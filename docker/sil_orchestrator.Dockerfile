# SIL Orchestrator — FastAPI REST plane.
# Spec: docs/Design/SIL/2026-05-12-sil-architecture-design.md §1 orchestration plane
FROM python:3.11-slim

WORKDIR /opt/sil

# Runtime deps — keep slim; protobuf+pyyaml only on top of FastAPI stack.
RUN pip install --no-cache-dir \
    fastapi==0.115.4 \
    uvicorn[standard]==0.32.0 \
    pydantic==2.9.2 \
    protobuf==5.28.2 \
    pyyaml==6.0.2

# Copy orchestrator package only (sim_workbench / l3 kernel are separate services)
COPY src/sil_orchestrator /opt/sil/sil_orchestrator

# Persistent directories — compose mounts /var/sil/{scenarios,runs,exports}
RUN mkdir -p /var/sil/scenarios /var/sil/runs /var/sil/exports

ENV PYTHONPATH=/opt/sil

EXPOSE 8000

CMD ["python", "-m", "uvicorn", "sil_orchestrator.main:app", \
     "--host", "0.0.0.0", "--port", "8000"]
