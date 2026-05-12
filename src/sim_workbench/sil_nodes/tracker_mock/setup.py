from setuptools import setup

package_name = 'tracker_mock'

setup(
    name=package_name,
    version='0.1.0',
    packages=[package_name],
    data_files=[
        ('share/ament_index/resource_index/packages',
         ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='Marine',
    maintainer_email='marine@example.com',
    description='Tracker Mock Node — God pass-through + KF tracker stubs',
    license='Apache-2.0',
    entry_points={
        'console_scripts': [
            'tracker_mock_node = tracker_mock.node:main',
        ],
    },
)
