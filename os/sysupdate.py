"""
YouOS System Update Service
sysupdate.py - Background service for checking and installing system updates
"""

import sys
import json
import time
import requests
import hashlib
from pathlib import Path
from PyQt6.QtCore import QThread, pyqtSignal, QTimer
from PyQt6.QtWidgets import QWidget, QVBoxLayout, QLabel, QProgressBar, QApplication
from PyQt6.QtGui import QPixmap, QPainter, QLinearGradient, QColor
from PyQt6.QtCore import Qt

# Configuration
GITHUB_REPO = "https://raw.githubusercontent.com/yousufyasser2005/YouOS/main/os"
BASE_DIR = Path("/home/yousuf-yasser-elshaer/codes/os")
SYSTEM_DATA_URL = f"{GITHUB_REPO}/systemdata.json"
LOCAL_SYSTEM_DATA = BASE_DIR / "systemdata.json"
CHECK_INTERVAL = 3600000  # 1 hour in milliseconds

# System modules that require full restart
SYSTEM_MODULES = [
    "main.py",
    "desktop.py", 
    "start.py",
    "widgets.py",
    "utils.py",
    "auth.py"
]

# Builtin programs that can be updated without restart
BUILTIN_PROGRAMS = [
    "browser.py",
    "file_manager.py",
    "media_viewer.py",
    "text_editor.py",
    "calculator.py",
    "settings.py",
    "terminal.py",
    "task_manager.py",
    "recycle_bin.py",
    "app_store.py"
]

COLORS = {
    'bg_primary': '#0f0f1e',
    'accent_primary': '#3b82f6',
    'text_primary': '#ffffff',
    'text_secondary': '#9ca3af',
}


class UpdateChecker(QThread):
    """Background thread for checking updates"""
    
    updates_available = pyqtSignal(dict)  # Emits {program: version}
    check_complete = pyqtSignal(bool)  # Emits success status
    
    def __init__(self):
        super().__init__()
        self.running = True
    
    def run(self):
        """Check for updates periodically"""
        while self.running:
            try:
                updates = self.check_for_updates()
                if updates:
                    self.updates_available.emit(updates)
                self.check_complete.emit(True)
            except Exception as e:
                print(f"❌ Update check failed: {e}")
                self.check_complete.emit(False)
            
            # Wait for next check
            time.sleep(CHECK_INTERVAL / 1000)
    
    def check_for_updates(self):
        """Check for available updates"""
        try:
            # Fetch remote system data
            response = requests.get(SYSTEM_DATA_URL, timeout=10)
            response.raise_for_status()
            remote_data = response.json()
            
            # Load local system data
            if LOCAL_SYSTEM_DATA.exists():
                with open(LOCAL_SYSTEM_DATA, 'r') as f:
                    local_data = json.load(f)
            else:
                # Create initial system data file
                local_data = {"version": "1.0.0", "programs": {}, "system_modules": {}}
                self.save_local_data(local_data)
            
            # Compare versions
            updates = {}
            
            # Check programs
            remote_programs = remote_data.get("programs", {})
            local_programs = local_data.get("programs", {})
            
            for program, remote_version in remote_programs.items():
                local_version = local_programs.get(program, "0.0.0")
                if self.compare_versions(remote_version, local_version) > 0:
                    updates[program] = remote_version
            
            # Check system modules
            remote_modules = remote_data.get("system_modules", {})
            local_modules = local_data.get("system_modules", {})
            
            for module, remote_version in remote_modules.items():
                local_version = local_modules.get(module, "0.0.0")
                if self.compare_versions(remote_version, local_version) > 0:
                    updates[module] = remote_version
            
            return updates
            
        except Exception as e:
            print(f"❌ Error checking updates: {e}")
            return {}
    
    def compare_versions(self, version1, version2):
        """Compare two version strings (e.g., '1.2.3' vs '1.2.2')"""
        try:
            v1_parts = [int(x) for x in version1.split('.')]
            v2_parts = [int(x) for x in version2.split('.')]
            
            # Pad with zeros if lengths differ
            max_len = max(len(v1_parts), len(v2_parts))
            v1_parts.extend([0] * (max_len - len(v1_parts)))
            v2_parts.extend([0] * (max_len - len(v2_parts)))
            
            for v1, v2 in zip(v1_parts, v2_parts):
                if v1 > v2:
                    return 1
                elif v1 < v2:
                    return -1
            return 0
        except:
            return 0
    
    def save_local_data(self, data):
        """Save local system data"""
        try:
            with open(LOCAL_SYSTEM_DATA, 'w') as f:
                json.dump(data, f, indent=4)
        except Exception as e:
            print(f"❌ Error saving local data: {e}")
    
    def stop(self):
        """Stop the update checker"""
        self.running = False


class UpdateDownloader(QThread):
    """Thread for downloading and installing updates"""
    
    progress_update = pyqtSignal(str, int)  # Emits (message, progress)
    update_complete = pyqtSignal(bool, str)  # Emits (success, message)
    
    def __init__(self, updates):
        super().__init__()
        self.updates = updates
    
    def run(self):
        """Download and install updates"""
        try:
            total_updates = len(self.updates)
            completed = 0
            
            # Categorize updates
            program_updates = []
            system_updates = []
            
            for filename in self.updates.keys():
                if filename in BUILTIN_PROGRAMS:
                    program_updates.append(filename)
                elif filename in SYSTEM_MODULES:
                    system_updates.append(filename)
            
            # Update programs first
            for program in program_updates:
                self.progress_update.emit(f"Updating {program}...", 
                                         int((completed / total_updates) * 100))
                
                if self.download_and_replace(program):
                    completed += 1
                else:
                    self.update_complete.emit(False, f"Failed to update {program}")
                    return
            
            # Update system modules
            for module in system_updates:
                self.progress_update.emit(f"Updating {module}...", 
                                         int((completed / total_updates) * 100))
                
                if self.download_and_replace(module):
                    completed += 1
                else:
                    self.update_complete.emit(False, f"Failed to update {module}")
                    return
            
            # Update local system data
            self.update_local_system_data()
            
            self.progress_update.emit("Update complete!", 100)
            
            # Check if system restart is needed
            if system_updates:
                self.update_complete.emit(True, "system_restart")
            else:
                self.update_complete.emit(True, "success")
                
        except Exception as e:
            print(f"❌ Update failed: {e}")
            self.update_complete.emit(False, str(e))
    
    def download_and_replace(self, filename):
        """Download file from GitHub and replace local file"""
        try:
            # Download file
            url = f"{GITHUB_REPO}/{filename}"
            response = requests.get(url, timeout=30)
            response.raise_for_status()
            
            # Backup old file
            local_file = BASE_DIR / filename
            if local_file.exists():
                backup_file = BASE_DIR / f"{filename}.backup"
                import shutil
                shutil.copy2(local_file, backup_file)
            
            # Write new file
            with open(local_file, 'wb') as f:
                f.write(response.content)
            
            print(f"✅ Updated {filename}")
            return True
            
        except Exception as e:
            print(f"❌ Failed to update {filename}: {e}")
            return False
    
    def update_local_system_data(self):
        """Update local system data with new versions"""
        try:
            # Download latest system data
            response = requests.get(SYSTEM_DATA_URL, timeout=10)
            response.raise_for_status()
            remote_data = response.json()
            
            # Save to local
            with open(LOCAL_SYSTEM_DATA, 'w') as f:
                json.dump(remote_data, f, indent=4)
            
        except Exception as e:
            print(f"❌ Failed to update system data: {e}")


class ProgramUpdateScreen(QWidget):
    """Popup screen for program updates"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowFlags(Qt.WindowType.FramelessWindowHint | Qt.WindowType.WindowStaysOnTopHint)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setFixedSize(500, 250)
        self.setup_ui()
    
    def setup_ui(self):
        """Setup UI"""
        layout = QVBoxLayout(self)
        layout.setContentsMargins(40, 40, 40, 40)
        layout.setSpacing(20)
        
        # Semi-transparent background
        self.setStyleSheet(f"""
            QWidget {{
                background: rgba(15, 15, 30, 0.95);
                border: 2px solid rgba(59, 130, 246, 0.5);
                border-radius: 16px;
            }}
        """)
        
        # Title
        title = QLabel("Updating YouOS")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        title.setStyleSheet(f"""
            color: {COLORS['text_primary']};
            font-size: 24px;
            font-weight: bold;
            background: transparent;
        """)
        layout.addWidget(title)
        
        # Status label
        self.status_label = QLabel("Preparing updates...")
        self.status_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.status_label.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 14px;
            background: transparent;
        """)
        layout.addWidget(self.status_label)
        
        # Progress bar
        self.progress = QProgressBar()
        self.progress.setFixedHeight(10)
        self.progress.setTextVisible(False)
        self.progress.setStyleSheet(f"""
            QProgressBar {{
                background: rgba(255, 255, 255, 0.1);
                border-radius: 5px;
                border: none;
            }}
            QProgressBar::chunk {{
                background: {COLORS['accent_primary']};
                border-radius: 5px;
            }}
        """)
        layout.addWidget(self.progress)
        
        layout.addStretch()
    
    def update_progress(self, message, progress):
        """Update progress display"""
        self.status_label.setText(message)
        self.progress.setValue(progress)


class SystemUpdateScreen(QWidget):
    """Full-screen update screen for system module updates"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowFlags(Qt.WindowType.FramelessWindowHint | Qt.WindowType.WindowStaysOnTopHint)
        self.showFullScreen()
        self.setup_ui()
    
    def setup_ui(self):
        """Setup UI"""
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        
        # Logo
        self.logo_label = QLabel()
        logo_path = Path("/home/yousuf-yasser-elshaer/codes/os/assets/start.png")
        if logo_path.exists():
            pixmap = QPixmap(str(logo_path))
            if not pixmap.isNull():
                scaled_pixmap = pixmap.scaled(200, 200, 
                                             Qt.AspectRatioMode.KeepAspectRatio, 
                                             Qt.TransformationMode.SmoothTransformation)
                self.logo_label.setPixmap(scaled_pixmap)
        else:
            self.logo_label.setText("YouOS")
            self.logo_label.setStyleSheet(f"""
                color: {COLORS['text_primary']};
                font-size: 64px;
                font-weight: bold;
            """)
        self.logo_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.logo_label)
        
        layout.addSpacing(30)
        
        # Progress bar
        self.progress = QProgressBar()
        self.progress.setFixedWidth(400)
        self.progress.setFixedHeight(8)
        self.progress.setTextVisible(False)
        self.progress.setStyleSheet(f"""
            QProgressBar {{
                background: rgba(255, 255, 255, 0.1);
                border-radius: 4px;
                border: none;
            }}
            QProgressBar::chunk {{
                background: {COLORS['accent_primary']};
                border-radius: 4px;
            }}
        """)
        layout.addWidget(self.progress, alignment=Qt.AlignmentFlag.AlignCenter)
        
        layout.addSpacing(15)
        
        # Status label
        self.status_label = QLabel("Updating system...")
        self.status_label.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 14px;
        """)
        self.status_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.status_label)
    
    def paintEvent(self, event):
        """Custom paint event for black background"""
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor(0, 0, 0))
    
    def update_progress(self, message, progress):
        """Update progress display"""
        self.status_label.setText(message)
        self.progress.setValue(progress)


class UpdateManager:
    """Main update manager that coordinates update checking and installation"""
    
    def __init__(self, desktop_manager):
        self.desktop_manager = desktop_manager
        self.update_checker = None
        self.update_downloader = None
        self.update_screen = None
        self.pending_updates = {}
        
        # Start update checker
        self.start_update_checker()
    
    def start_update_checker(self):
        """Start background update checker"""
        self.update_checker = UpdateChecker()
        self.update_checker.updates_available.connect(self.on_updates_available)
        self.update_checker.check_complete.connect(self.on_check_complete)
        self.update_checker.start()
        print("✅ Update checker started")
    
    def on_updates_available(self, updates):
        """Handle available updates"""
        if not updates:
            return
        
        self.pending_updates = updates
        
        # Send notification
        update_list = "\n".join([f"• {name} → v{version}" 
                                for name, version in updates.items()])
        message = f"Updates available:\n{update_list}"
        
        self.desktop_manager.add_notification(
            "System Updates Available",
            message,
            "🔄",
            "Now"
        )
        
        print(f"✅ Found {len(updates)} updates")
    
    def on_check_complete(self, success):
        """Handle check completion"""
        if success:
            print("✅ Update check completed")
        else:
            print("❌ Update check failed")
    
    def install_updates(self):
        """Install pending updates"""
        if not self.pending_updates:
            print("No updates to install")
            return
        
        # Categorize updates
        has_system_updates = any(f in SYSTEM_MODULES for f in self.pending_updates.keys())
        
        # Show appropriate update screen
        if has_system_updates:
            self.update_screen = SystemUpdateScreen()
        else:
            self.update_screen = ProgramUpdateScreen(self.desktop_manager)
            # Center on screen
            parent_rect = self.desktop_manager.rect()
            x = (parent_rect.width() - 500) // 2
            y = (parent_rect.height() - 250) // 2
            self.update_screen.move(x, y)
        
        self.update_screen.show()
        
        # Start download thread
        self.update_downloader = UpdateDownloader(self.pending_updates)
        self.update_downloader.progress_update.connect(self.update_screen.update_progress)
        self.update_downloader.update_complete.connect(self.on_update_complete)
        self.update_downloader.start()
        
        print("✅ Started installing updates")
    
    def on_update_complete(self, success, message):
        """Handle update completion"""
        if success:
            if message == "system_restart":
                # System modules updated - restart required
                self.update_screen.update_progress("Restarting YouOS...", 100)
                QTimer.singleShot(2000, self.restart_system)
            else:
                # Program updates only - just close screen
                self.update_screen.update_progress("Update complete!", 100)
                QTimer.singleShot(1500, self.update_screen.close)
                
                # Show success notification
                self.desktop_manager.add_notification(
                    "Update Complete",
                    "All updates have been installed successfully",
                    "✅",
                    "Now"
                )
        else:
            # Update failed
            self.update_screen.update_progress(f"Update failed: {message}", 0)
            QTimer.singleShot(3000, self.update_screen.close)
            
            # Show error notification
            self.desktop_manager.add_notification(
                "Update Failed",
                f"Update failed: {message}",
                "❌",
                "Now"
            )
        
        # Clear pending updates
        self.pending_updates = {}
    
    def restart_system(self):
        """Restart YouOS after system update"""
        # Close update screen
        if self.update_screen:
            self.update_screen.close()
        
        # Restart without playing shutdown sound
        import os
        python = sys.executable
        os.execl(python, python, *sys.argv)
    
    def stop(self):
        """Stop update manager"""
        if self.update_checker:
            self.update_checker.stop()
            self.update_checker.wait()


def initialize_system_data():
    """Initialize systemdata.json if it doesn't exist"""
    if not LOCAL_SYSTEM_DATA.exists():
        initial_data = {
            "version": "1.0.0",
            "programs": {
                "browser.py": "1.0.0",
                "file_manager.py": "1.0.0",
                "media_viewer.py": "1.0.0",
                "text_editor.py": "1.0.0",
                "calculator.py": "1.0.0",
                "settings.py": "1.0.0",
                "terminal.py": "1.0.0",
                "task_manager.py": "1.0.0",
                "recycle_bin.py": "1.0.0",
                "app_store.py": "1.0.0"
            },
            "system_modules": {
                "main.py": "1.0.0",
                "desktop.py": "1.0.0",
                "start.py": "1.0.0",
                "widgets.py": "1.0.0",
                "utils.py": "1.0.0",
                "auth.py": "1.0.0"
            }
        }
        
        try:
            with open(LOCAL_SYSTEM_DATA, 'w') as f:
                json.dump(initial_data, f, indent=4)
            print("✅ Initialized systemdata.json")
        except Exception as e:
            print(f"❌ Failed to initialize systemdata.json: {e}")


if __name__ == "__main__":
    # Initialize system data if needed
    initialize_system_data()