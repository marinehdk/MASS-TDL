from setuptools import setup

package_name = 'ship_utils'

setup(
    name=package_name,
    version='0.0.1',
    packages=[package_name],
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='User',
    maintainer_email='user@example.com',
    description='Ship utils package 存放 Python 脚本，用于日志处理和绘图',
    license='Apache-2.0',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [
            'log_processor = ship_utils.log_processor:main',
            'plotter = ship_utils.plotter:main',
        ],
    },
)