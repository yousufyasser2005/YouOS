"""
YouOS Enhanced Kernel - v10.26m1.7.3
Advanced kernel with logging, IPC, and resource management
"""

import os
import sys
import time
import json
import logging
import threading
import queue
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Optional, Any

# Configure advanced logging
class KernelLogger:
    """Advanced kernel logging system"""
    
    def __init__(self):
        self.log_dir = Path('/var/log/youos')
        self.log_dir.mkdir(parents=True, exist_ok=True)
        
        # Create multiple log handlers
        self.kernel_log = self.log_dir / 'kernel.log'
        self.boot_log = self.log_dir / 'boot.log'
        self.service_log = self.log_dir / 'services.log'
        self.error_log = self.log_dir / 'errors.log'
        
        # Setup logging
        logging.basicConfig(
            level=logging.DEBUG,
            format='[%(asctime)s] [%(levelname)s] [%(name)s] %(message)s',
            datefmt='%Y-%m-%d %H:%M:%S'
        )
        
        # Create separate loggers
        self.logger = logging.getLogger('kernel')
        self.boot_logger = logging.getLogger('boot')
        self.service_logger = logging.getLogger('service')
        
        # Add file handlers
        self._add_file_handler(self.logger, self.kernel_log)
        self._add_file_handler(self.boot_logger, self.boot_log)
        self._add_file_handler(self.service_logger, self.service_log)
    
    def _add_file_handler(self, logger, log_file):
        """Add file handler to logger"""
        try:
            handler = logging.FileHandler(log_file)
            handler.setFormatter(logging.Formatter(
                '[%(asctime)s] [%(levelname)s] %(message)s'
            ))
            logger.addHandler(handler)
        except PermissionError:
            print(f"Warning: Cannot write to {log_file}")
    
    def log_boot(self, message, level='info'):
        """Log boot message"""
        getattr(self.boot_logger, level)(message)
    
    def log_kernel(self, message, level='info'):
        """Log kernel message"""
        getattr(self.logger, level)(message)
    
    def log_service(self, message, level='info'):
        """Log service message"""
        getattr(self.service_logger, level)(message)


class InterProcessCommunication:
    """IPC system for process communication"""
    
    def __init__(self):
        self.message_queues = {}
        self.shared_memory = {}
        self.locks = {}
        self.semaphores = {}
    
    def create_queue(self, name: str):
        """Create a message queue"""
        if name not in self.message_queues:
            self.message_queues[name] = queue.Queue()
            return True
        return False
    
    def send_message(self, queue_name: str, message: Any, priority: int = 0):
        """Send message to queue"""
        if queue_name in self.message_queues:
            self.message_queues[queue_name].put((priority, time.time(), message))
            return True
        return False
    
    def receive_message(self, queue_name: str, timeout: Optional[float] = None):
        """Receive message from queue"""
        if queue_name in self.message_queues:
            try:
                priority, timestamp, message = self.message_queues[queue_name].get(
                    timeout=timeout
                )
                return message
            except queue.Empty:
                return None
        return None
    
    def create_shared_memory(self, name: str, initial_value: Any = None):
        """Create shared memory segment"""
        if name not in self.shared_memory:
            self.shared_memory[name] = initial_value
            self.locks[name] = threading.Lock()
            return True
        return False
    
    def write_shared_memory(self, name: str, value: Any):
        """Write to shared memory"""
        if name in self.shared_memory:
            with self.locks[name]:
                self.shared_memory[name] = value
                return True
        return False
    
    def read_shared_memory(self, name: str):
        """Read from shared memory"""
        if name in self.shared_memory:
            with self.locks[name]:
                return self.shared_memory[name]
        return None


class ResourceMonitor:
    """System resource monitoring and throttling"""
    
    def __init__(self):
        self.resource_limits = {
            'cpu': {'warning': 80, 'critical': 95},
            'memory': {'warning': 80, 'critical': 95},
            'disk': {'warning': 85, 'critical': 95},
            'temperature': {'warning': 70, 'critical': 85}
        }
        self.monitoring = False
        self.monitor_thread = None
        self.callbacks = []
    
    def start_monitoring(self, interval: int = 5):
        """Start resource monitoring"""
        if not self.monitoring:
            self.monitoring = True
            self.monitor_thread = threading.Thread(
                target=self._monitor_loop,
                args=(interval,),
                daemon=True
            )
            self.monitor_thread.start()
    
    def stop_monitoring(self):
        """Stop resource monitoring"""
        self.monitoring = False
        if self.monitor_thread:
            self.monitor_thread.join(timeout=2)
    
    def _monitor_loop(self, interval: int):
        """Monitoring loop"""
        try:
            import psutil
            
            while self.monitoring:
                # Check CPU
                cpu_percent = psutil.cpu_percent(interval=1)
                if cpu_percent >= self.resource_limits['cpu']['critical']:
                    self._trigger_callbacks('cpu', cpu_percent, 'critical')
                elif cpu_percent >= self.resource_limits['cpu']['warning']:
                    self._trigger_callbacks('cpu', cpu_percent, 'warning')
                
                # Check memory
                memory = psutil.virtual_memory()
                if memory.percent >= self.resource_limits['memory']['critical']:
                    self._trigger_callbacks('memory', memory.percent, 'critical')
                elif memory.percent >= self.resource_limits['memory']['warning']:
                    self._trigger_callbacks('memory', memory.percent, 'warning')
                
                # Check disk
                disk = psutil.disk_usage('/')
                if disk.percent >= self.resource_limits['disk']['critical']:
                    self._trigger_callbacks('disk', disk.percent, 'critical')
                elif disk.percent >= self.resource_limits['disk']['warning']:
                    self._trigger_callbacks('disk', disk.percent, 'warning')
                
                # Check temperature
                try:
                    temps = psutil.sensors_temperatures()
                    if temps:
                        for name, entries in temps.items():
                            for entry in entries:
                                if entry.current >= self.resource_limits['temperature']['critical']:
                                    self._trigger_callbacks('temperature', entry.current, 'critical')
                                elif entry.current >= self.resource_limits['temperature']['warning']:
                                    self._trigger_callbacks('temperature', entry.current, 'warning')
                except:
                    pass
                
                time.sleep(interval)
        except ImportError:
            print("Warning: psutil not available for resource monitoring")
    
    def _trigger_callbacks(self, resource: str, value: float, level: str):
        """Trigger resource alert callbacks"""
        for callback in self.callbacks:
            try:
                callback(resource, value, level)
            except Exception as e:
                print(f"Error in resource callback: {e}")
    
    def add_callback(self, callback):
        """Add resource alert callback"""
        self.callbacks.append(callback)


class EnhancedYouOSKernel:
    """Enhanced YouOS Kernel with advanced features"""
    
    VERSION = "10.26m1.7.3"
    BUILD_DATE = "2026-01-12"
    
    def __init__(self):
        # Initialize logger
        self.logger = KernelLogger()
        self.logger.log_kernel(f"Initializing YouOS Kernel {self.VERSION}")
        
        # Core components
        self.boot_manager = None
        self.package_manager = None
        self.process_manager = None
        self.hardware_manager = None
        self.service_manager = None
        self.security_manager = None
        self.network_manager = None
        self.config = None
        
        # Advanced features
        self.ipc = InterProcessCommunication()
        self.resource_monitor = ResourceMonitor()
        
        # Kernel state
        self.boot_time = None
        self.running = False
        self.components_loaded = {}
        
        # Performance metrics
        self.metrics = {
            'boot_time': 0,
            'uptime': 0,
            'process_count': 0,
            'service_count': 0,
            'component_errors': 0
        }
        
        # Setup resource monitoring callbacks
        self.resource_monitor.add_callback(self._handle_resource_alert)
    
    def _handle_resource_alert(self, resource: str, value: float, level: str):
        """Handle resource alerts"""
        message = f"Resource alert: {resource} at {value:.1f}% ({level})"
        
        if level == 'critical':
            self.logger.log_kernel(message, 'error')
            # Take action for critical resources
            if resource == 'memory':
                self._emergency_memory_cleanup()
            elif resource == 'temperature':
                self._emergency_cooling()
        else:
            self.logger.log_kernel(message, 'warning')
    
    def _emergency_memory_cleanup(self):
        """Emergency memory cleanup"""
        self.logger.log_kernel("Initiating emergency memory cleanup", 'warning')
        # Trigger garbage collection, close unnecessary processes, etc.
        import gc
        gc.collect()
    
    def _emergency_cooling(self):
        """Emergency cooling measures"""
        self.logger.log_kernel("Initiating emergency cooling", 'error')
        # Reduce CPU frequency, stop non-critical processes, etc.
        pass
    
    def initialize(self) -> bool:
        """Initialize kernel and all components"""
        try:
            start_time = time.time()
            self.logger.log_boot("Starting kernel initialization")
            
            # Import components
            from system_config import SystemConfig
            from boot_manager import BootManager
            from package_manager import PackageManager
            from process_manager import ProcessManager
            from hardware_manager import HardwareManager
            from service_manager import ServiceManager
            from security_manager import SecurityManager
            from network_manager import NetworkManager
            
            # Initialize configuration first
            self.logger.log_boot("Loading system configuration")
            self.config = SystemConfig()
            self.components_loaded['config'] = True
            
            # Initialize hardware manager
            self.logger.log_boot("Initializing hardware manager")
            self.hardware_manager = HardwareManager()
            self.components_loaded['hardware'] = True
            
            # Initialize security manager
            self.logger.log_boot("Initializing security manager")
            self.security_manager = SecurityManager()
            self.components_loaded['security'] = True
            
            # Initialize process manager
            self.logger.log_boot("Initializing process manager")
            self.process_manager = ProcessManager()
            self.components_loaded['process'] = True
            
            # Initialize service manager
            self.logger.log_boot("Initializing service manager")
            self.service_manager = ServiceManager()
            self.components_loaded['service'] = True
            
            # Initialize package manager
            self.logger.log_boot("Initializing package manager")
            self.package_manager = PackageManager()
            self.components_loaded['package'] = True
            
            # Initialize boot manager
            self.logger.log_boot("Initializing boot manager")
            self.boot_manager = BootManager()
            self.components_loaded['boot'] = True
            
            # Initialize network manager
            self.logger.log_boot("Initializing network manager")
            self.network_manager = NetworkManager()
            self.components_loaded['network'] = True
            
            # Calculate boot time
            self.metrics['boot_time'] = time.time() - start_time
            self.boot_time = datetime.now()
            
            self.logger.log_boot(
                f"Kernel initialized successfully in {self.metrics['boot_time']:.2f}s"
            )
            
            return True
            
        except Exception as e:
            self.logger.log_kernel(f"Kernel initialization failed: {e}", 'error')
            self.metrics['component_errors'] += 1
            return False
    
    def boot(self) -> bool:
        """Boot the system"""
        try:
            self.logger.log_boot("Starting system boot sequence")
            
            if not self.boot_manager:
                self.logger.log_boot("Boot manager not initialized", 'error')
                return False
            
            # Execute boot sequence
            success = self.boot_manager.boot()
            
            if success:
                self.running = True
                self.logger.log_boot("System boot completed successfully")
                
                # Start resource monitoring
                self.resource_monitor.start_monitoring()
                
                # Start essential services
                self._start_essential_services()
                
                return True
            else:
                self.logger.log_boot("System boot failed", 'error')
                return False
                
        except Exception as e:
            self.logger.log_boot(f"Boot sequence error: {e}", 'error')
            return False
    
    def _start_essential_services(self):
        """Start essential system services"""
        essential_services = [
            'network',
            'dbus',
            'logging',
        ]
        
        for service_name in essential_services:
            try:
                if self.service_manager.start_service(service_name):
                    self.logger.log_service(f"Started {service_name}")
                    self.metrics['service_count'] += 1
            except Exception as e:
                self.logger.log_service(
                    f"Failed to start {service_name}: {e}",
                    'error'
                )
    
    def shutdown(self, mode: str = 'shutdown') -> bool:
        """Shutdown the system"""
        try:
            self.logger.log_kernel(f"Initiating system {mode}")
            
            # Stop resource monitoring
            self.resource_monitor.stop_monitoring()
            
            # Stop all services
            if self.service_manager:
                self.logger.log_service("Stopping all services")
                for service_name in self.service_manager.services.keys():
                    try:
                        self.service_manager.stop_service(service_name)
                    except Exception as e:
                        self.logger.log_service(
                            f"Error stopping {service_name}: {e}",
                            'warning'
                        )
            
            # Sync filesystems
            os.sync()
            
            self.running = False
            self.logger.log_kernel(f"System {mode} completed")
            
            return True
            
        except Exception as e:
            self.logger.log_kernel(f"Shutdown error: {e}", 'error')
            return False
    
    def get_system_info(self) -> Dict[str, Any]:
        """Get comprehensive system information"""
        info = {
            'kernel': {
                'version': self.VERSION,
                'build_date': self.BUILD_DATE,
                'boot_time': self.boot_time.isoformat() if self.boot_time else None,
                'uptime': self.get_uptime(),
                'running': self.running
            },
            'components': self.components_loaded,
            'metrics': self.metrics,
            'config': self.config.config if self.config else {}
        }
        
        # Add hardware info
        if self.hardware_manager:
            try:
                info['hardware'] = {
                    'cpu': self.hardware_manager.detect_cpu(),
                    'memory': self.hardware_manager.detect_memory(),
                    'disks': self.hardware_manager.detect_disks(),
                    'network': self.hardware_manager.detect_network(),
                    'gpu': self.hardware_manager.detect_gpu()
                }
            except:
                info['hardware'] = {'error': 'Hardware detection failed'}
        
        # Add network info
        if self.network_manager:
            try:
                info['network'] = {
                    'interfaces': [{
                        'name': iface.name,
                        'type': iface.type,
                        'status': iface.status,
                        'ip': iface.ip_address
                    } for iface in self.network_manager.get_interfaces()],
                    'stats': self.network_manager.get_network_stats()
                }
            except:
                info['network'] = {'error': 'Network detection failed'}
        
        return info
    
    def get_uptime(self) -> float:
        """Get system uptime in seconds"""
        if self.boot_time:
            return (datetime.now() - self.boot_time).total_seconds()
        return 0
    
    def health_check(self) -> Dict[str, Any]:
        """Perform system health check"""
        health = {
            'status': 'healthy',
            'issues': [],
            'warnings': []
        }
        
        # Check each component
        for component, loaded in self.components_loaded.items():
            if not loaded:
                health['issues'].append(f"{component} not loaded")
                health['status'] = 'degraded'
        
        # Check resource usage
        try:
            import psutil
            
            cpu = psutil.cpu_percent()
            memory = psutil.virtual_memory().percent
            disk = psutil.disk_usage('/').percent
            
            if cpu > 90:
                health['warnings'].append(f"High CPU usage: {cpu}%")
            if memory > 90:
                health['warnings'].append(f"High memory usage: {memory}%")
            if disk > 90:
                health['warnings'].append(f"High disk usage: {disk}%")
            
            if health['warnings']:
                health['status'] = 'warning'
        except:
            pass
        
        return health


# Global kernel instance
_kernel_instance = None

def get_kernel() -> EnhancedYouOSKernel:
    """Get global kernel instance"""
    global _kernel_instance
    if _kernel_instance is None:
        _kernel_instance = EnhancedYouOSKernel()
    return _kernel_instance


if __name__ == "__main__":
    # Test kernel
    kernel = get_kernel()
    
    print(f"YouOS Kernel {kernel.VERSION}")
    print("=" * 50)
    
    if kernel.initialize():
        print("✓ Kernel initialized")
        
        # Display system info
        info = kernel.get_system_info()
        print(f"\nBoot time: {info['metrics']['boot_time']:.2f}s")
        print(f"Components loaded: {sum(info['components'].values())}/{len(info['components'])}")
        
        # Health check
        health = kernel.health_check()
        print(f"\nHealth status: {health['status']}")
        if health['issues']:
            print("Issues:", health['issues'])
        if health['warnings']:
            print("Warnings:", health['warnings'])
    else:
        print("✗ Kernel initialization failed")