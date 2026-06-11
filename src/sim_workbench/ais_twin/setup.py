import os
from glob import glob

from setuptools import find_packages, setup

package_name = 'ais_twin'

setup(
    name=package_name,
    version='0.1.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        (os.path.join('share', package_name, 'config'),
            glob('config/*.yaml')),
        (os.path.join('share', package_name, 'launch'),
            glob('launch/*.py')),
    ],
    install_requires=['setuptools', 'pyyaml', 'websockets'],
    zip_safe=True,
    maintainer='Team-E3',
    maintainer_email='team-e3@mass-l3-tdl.local',
    description='AIS digital-twin capture and replay for MASS-L3 SIL',
    license='Proprietary',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [
            'ais_twin_capture = ais_twin.capture_cli:main',
            'ais_twin_replay_node = ais_twin.replay_node:main',
            'ais_twin_debug_api = ais_twin.debug_api:main',
        ],
    },
)
