"""
YouOS Process Manager
"""
import os
import signal
import time
import subprocess

class ProcessManager:
    """Manages system processes"""
    
    def __init__(self):
        self.processes = {}
        self.load_processes()
    
    def load_processes(self):
        """Load all running processes"""
        try:
            import psutil
            for proc in psutil.process_iter(['pid', 'name', 'username', 'memory_percent', 'cpu_percent']):
                try:
                    self.processes[proc.pid] = proc.info
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    pass
        except ImportError:
            # Fallback without psutil
            try:
                result = subprocess.run(['ps', 'aux'], capture_output=True, text=True)
                for line in result.stdout.split('\n')[1:]:
                    if line.strip():
                        parts = line.split()
                        if len(parts) >= 11:
                            pid = int(parts[1])
                            self.processes[pid] = {
                                'pid': pid,
                                'name': parts[10],
                                'username': parts[0]
                            }
            except:
                pass
    
    def spawn_process(self, command, args=None, env=None, priority=0):
        """Spawn a new process"""
        try:
            cmd = [command]
            if args:
                cmd.extend(args)
            
            process_env = os.environ.copy()
            if env:
                process_env.update(env)
            
            proc = subprocess.Popen(cmd, env=process_env)
            
            # Set priority if possible
            try:
                os.setpriority(os.PRIO_PROCESS, proc.pid, priority)
            except:
                pass
            
            self.processes[proc.pid] = {
                'pid': proc.pid,
                'command': command,
                'started_at': time.time()
            }
            
            return proc.pid
        except Exception as e:
            print(f"Failed to spawn process: {e}")
            return None
    
    def kill_process(self, pid, force=False):
        """Kill a process"""
        sig = signal.SIGKILL if force else signal.SIGTERM
        try:
            os.kill(pid, sig)
            if pid in self.processes:
                del self.processes[pid]
            return True
        except ProcessLookupError:
            return False
        except PermissionError:
            print(f"Permission denied to kill process {pid}")
            return False
    
    def set_priority(self, pid, priority):
        """Set process priority (-20 to 19)"""
        try:
            os.setpriority(os.PRIO_PROCESS, pid, priority)
            return True
        except:
            return False
    
    def get_process_info(self, pid):
        """Get process information"""
        return self.processes.get(pid)
    
    def list_processes(self):
        """List all processes"""
        return list(self.processes.values())
    
    def find_processes_by_name(self, name):
        """Find processes by name"""
        return [proc for proc in self.processes.values() 
                if proc.get('name', '').lower() == name.lower()]