from setuptools import find_packages, setup

package_name = "scenario_authoring"

setup(
    name=package_name,
    version="0.1.0",
    packages=find_packages(exclude=["test"]),
    data_files=[
        ("share/" + package_name, ["package.xml"]),
        ("share/" + package_name + "/config", ["scenario_authoring/config/scenario_authoring.yaml"]),
        ("share/" + package_name + "/launch", ["launch/scenario_authoring.launch.py"]),
    ],
    install_requires=["setuptools"],
    zip_safe=True,
    maintainer="Tech Lead",
    maintainer_email="tech-lead@mass-l3.local",
    description="AIS-driven maritime scenario authoring pipeline",
    license="Proprietary",
    tests_require=["pytest"],
    entry_points={
        "console_scripts": [
            "ais_replay_node = scenario_authoring.replay.ais_replay_node:main",
            "extract_encounters = scenario_authoring.authoring.encounter_extractor:main",
        ],
    },
)
