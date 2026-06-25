import os
from glob import glob
from setuptools import setup, find_packages

setup(
    name='route_planning_ros2',
    version='0.1.0',
    packages=find_packages(),
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/route_planning_ros2']),
        ('share/route_planning_ros2', ['package.xml']),
        (os.path.join('share', 'route_planning_ros2', 'launch'), glob('launch/*.py')),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    entry_points={
        'console_scripts': [
            'gnc_sim_node = route_planning_ros2.gnc_sim_node:main',
        ],
    },
)
