from setuptools import setup, find_packages
import os
from glob import glob

package_name = 'ais_bridge'

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
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='Team-E3',
    maintainer_email='team-e3@mass-l3-tdl.local',
    description='AIS historical data bridge for D1.3a SIL',
    license='Proprietary',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [
            'ais_replay_node = ais_bridge.replay_node:main',
        ],
    },
)
