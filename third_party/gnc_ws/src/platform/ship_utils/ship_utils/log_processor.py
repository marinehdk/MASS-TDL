#!/usr/bin/env python3
"""
Log processor for ship simulation data
"""

import argparse
import csv
import json
from datetime import datetime

def process_log_file(input_file, output_file):
    """Process log file and generate summary"""
    print(f"Processing log file: {input_file}")
    
    # 读取日志文件
    data = []
    with open(input_file, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            data.append(row)
    
    # 生成摘要
    summary = {
        'total_records': len(data),
        'start_time': data[0]['timestamp'] if data else None,
        'end_time': data[-1]['timestamp'] if data else None,
        'processed_at': datetime.now().isoformat()
    }
    
    # 保存摘要
    with open(output_file, 'w') as f:
        json.dump(summary, f, indent=2)
    
    print(f"Summary saved to: {output_file}")

def main():
    """Main function"""
    parser = argparse.ArgumentParser(description='Process ship simulation log files')
    parser.add_argument('input_file', help='Input log file path')
    parser.add_argument('output_file', help='Output summary file path')
    args = parser.parse_args()
    
    process_log_file(args.input_file, args.output_file)

if __name__ == '__main__':
    main()