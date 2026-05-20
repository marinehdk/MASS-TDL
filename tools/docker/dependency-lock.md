# MASS L3 TDL — Dependency Version Lock Table

> Generated: 2026-05-20 | Container runtime: OrbStack | Group: mass-l3-tacticallayer
> Update: run `bash tools/docker/audit-versions.sh` → review → commit version-lock.txt → rebuild

## Base Image
| Image | Digest |
|---|---|
| ros:humble-ros-base | sha256:5417e56962ff6e15d4cf9b2f78a71a78f3901f47cd78696b575b7eecdb54eb78 |

## System Packages (apt)
| Package | Version |
|---|---|
| gcc-12 | 12.3.0-1ubuntu1~22.04.3 |
| clang-tidy-14 | 1:14.0.0-1ubuntu1.1 |
| cmake | 3.22.1-1ubuntu1.22.04.2 |
| cppcheck | 2.7-1 |
| libeigen3-dev | 3.4.0-2ubuntu2 |
| libgeographiclib-dev | 1.52-1 |
| libboost-dev | 1.74.0.3ubuntu7 |
| nlohmann-json3-dev | 3.10.5-2 |

## Python Packages (pip)
| Package | Version |
|---|---|
| colcon-core | 0.20.1 |
| ruff | 0.6.9 |
| mypy | 1.10.1 |
| pytest | 8.3.3 |
| pytest-cov | 5.0.0 |
| casadi | 3.7.2 |
| empy | 3.3.4 |
| farn | (latest) |
| ospx | (latest) |
| pythonfmu | (latest) |
| pyyaml | (latest) |
| colcon-common-extensions | (no PyPI release tag — known exception) |

## Known Exceptions
- `colcon-common-extensions`: no PyPI release tag — not pinned