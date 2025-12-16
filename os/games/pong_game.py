import os
import subprocess
import sys

def open_pong_game(root=None):
    # Use absolute path to ensure it's found
    pong_path = '/home/yousuf-yasser-elshaer/codes/os/games/pong.py'
    if not os.path.exists(pong_path):
        print(f"Error: Pong file not found at {pong_path}")
        return None
    subprocess.Popen([sys.executable, pong_path])
    return None