#!/usr/bin/env python3
"""
YouOS 10 - Task Manager Application
Shows running YouOS applications and allows management
"""

import sys
import psutil
from pathlib import Path
from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                              QHBoxLayout, QTableWidget, QTableWidgetItem, 
                              QPushButton, QLabel, QTabWidget, QHeaderView,
                              QMessageBox, QLineEdit, QFrame)
from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QFont

COLORS = {
    'bg_primary': '#0f0f1e',
    'bg_secondary': '#1a1a2e',
    'bg_tertiary': '#252538',
    'accent_primary': '#3b82f6',
    'accent_hover': '#60a5fa',
    'text_primary': '#ffffff',
    'text_secondary': '#9ca3af',
    'border': '#374151',
}


class TaskManager(QMainWindow):
    def __init__(self, desktop_manager=None):
        super().__init__()
        self.desktop_manager = desktop_manager
        self.init_ui()
        self.center_window()
        
        # Auto-refresh timer
        self.refresh_timer = QTimer()
        self.refresh_timer.timeout.connect(self.refresh_processes)
        self.refresh_timer.start(2000)  # Refresh every 2 seconds
        
        self.refresh_processes()
    
    def init_ui(self):
        self.setWindowTitle("Task Manager - YouOS 10")
        self.setGeometry(100, 100, 900, 600)
        
        # Apply dark theme
        self.setStyleSheet(f"""
            QMainWindow {{
                background-color: {COLORS['bg_primary']};
            }}
            QWidget {{
                background-color: {COLORS['bg_primary']};
                color: {COLORS['text_primary']};
            }}
            QTabWidget::pane {{
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                background: {COLORS['bg_secondary']};
            }}
            QTabBar::tab {{
                background: {COLORS['bg_tertiary']};
                color: {COLORS['text_secondary']};
                padding: 10px 20px;
                margin: 2px;
                border-radius: 6px;
            }}
            QTabBar::tab:selected {{
                background: {COLORS['accent_primary']};
                color: {COLORS['text_primary']};
            }}
            QTableWidget {{
                background-color: {COLORS['bg_secondary']};
                color: {COLORS['text_primary']};
                gridline-color: {COLORS['border']};
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                selection-background-color: {COLORS['accent_primary']};
            }}
            QHeaderView::section {{
                background-color: {COLORS['bg_tertiary']};
                color: {COLORS['text_primary']};
                padding: 8px;
                border: none;
                font-weight: bold;
            }}
            QPushButton {{
                background-color: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 6px;
                padding: 8px 16px;
                font-size: 13px;
            }}
            QPushButton:hover {{
                background-color: {COLORS['accent_hover']};
            }}
            QPushButton:pressed {{
                background-color: #2563eb;
            }}
            QPushButton:disabled {{
                background-color: {COLORS['bg_tertiary']};
                color: {COLORS['text_secondary']};
            }}
            QLineEdit {{
                background: {COLORS['bg_tertiary']};
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 6px;
                padding: 8px;
            }}
            QLabel {{
                color: {COLORS['text_primary']};
            }}
        """)
        
        # Central widget
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        layout = QVBoxLayout(central_widget)
        layout.setContentsMargins(15, 15, 15, 15)
        layout.setSpacing(10)
        
        # Header
        header = QLabel("YouOS Task Manager")
        header.setStyleSheet(f"font-size: 20px; font-weight: bold; color: {COLORS['accent_primary']};")
        layout.addWidget(header)
        
        # Tab widget
        self.tab_widget = QTabWidget()
        layout.addWidget(self.tab_widget)
        
        # Processes tab
        self.create_processes_tab()
        
        # Performance tab
        self.create_performance_tab()
        
        # Status bar
        self.status_label = QLabel("Ready")
        self.status_label.setStyleSheet(f"color: {COLORS['text_secondary']}; padding: 5px;")
        layout.addWidget(self.status_label)
    
    def create_processes_tab(self):
        processes_widget = QWidget()
        layout = QVBoxLayout(processes_widget)
        layout.setContentsMargins(10, 10, 10, 10)
        
        # Search bar
        search_layout = QHBoxLayout()
        search_label = QLabel("Search:")
        search_layout.addWidget(search_label)
        
        self.search_input = QLineEdit()
        self.search_input.setPlaceholderText("Search processes...")
        self.search_input.textChanged.connect(self.filter_processes)
        search_layout.addWidget(self.search_input)
        layout.addLayout(search_layout)
        
        # Process table
        self.process_table = QTableWidget()
        self.process_table.setColumnCount(5)
        self.process_table.setHorizontalHeaderLabels(["Program", "Status", "PID", "CPU %", "Memory (MB)"])
        
        # Set column widths
        header = self.process_table.horizontalHeader()
        header.setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        header.setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        header.setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
        header.setSectionResizeMode(3, QHeaderView.ResizeMode.ResizeToContents)
        header.setSectionResizeMode(4, QHeaderView.ResizeMode.ResizeToContents)
        
        self.process_table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.process_table.setSelectionMode(QTableWidget.SelectionMode.SingleSelection)
        self.process_table.setAlternatingRowColors(True)
        layout.addWidget(self.process_table)
        
        # Buttons
        button_layout = QHBoxLayout()
        
        self.refresh_btn = QPushButton("🔄 Refresh")
        self.refresh_btn.clicked.connect(self.refresh_processes)
        button_layout.addWidget(self.refresh_btn)
        
        self.end_task_btn = QPushButton("❌ End Task")
        self.end_task_btn.clicked.connect(self.end_selected_task)
        button_layout.addWidget(self.end_task_btn)
        
        button_layout.addStretch()
        layout.addLayout(button_layout)
        
        self.tab_widget.addTab(processes_widget, "Processes")
    
    def create_performance_tab(self):
        performance_widget = QWidget()
        layout = QVBoxLayout(performance_widget)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(15)
        
        # System info frame
        info_frame = QFrame()
        info_frame.setStyleSheet(f"""
            QFrame {{
                background: {COLORS['bg_secondary']};
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                padding: 15px;
            }}
        """)
        info_layout = QVBoxLayout(info_frame)
        
        title = QLabel("System Performance")
        title.setStyleSheet("font-size: 16px; font-weight: bold;")
        info_layout.addWidget(title)
        
        self.cpu_label = QLabel("CPU Usage: --")
        self.cpu_label.setStyleSheet("font-size: 14px; padding: 5px;")
        info_layout.addWidget(self.cpu_label)
        
        self.memory_label = QLabel("Memory Usage: --")
        self.memory_label.setStyleSheet("font-size: 14px; padding: 5px;")
        info_layout.addWidget(self.memory_label)
        
        self.processes_count_label = QLabel("YouOS Processes: --")
        self.processes_count_label.setStyleSheet("font-size: 14px; padding: 5px;")
        info_layout.addWidget(self.processes_count_label)
        
        layout.addWidget(info_frame)
        layout.addStretch()
        
        self.tab_widget.addTab(performance_widget, "Performance")
    
    def center_window(self):
        """Center window on screen"""
        screen = self.screen().geometry()
        x = (screen.width() - self.width()) // 2
        y = (screen.height() - self.height()) // 2
        self.move(x, y)
    
    def get_youos_processes(self):
        """Get list of YouOS processes from desktop manager"""
        youos_processes = []
        
        if self.desktop_manager and hasattr(self.desktop_manager, 'running_processes'):
            for app_name, proc_list in self.desktop_manager.running_processes.items():
                for proc in proc_list:
                    try:
                        # Check if process is still alive
                        if proc.poll() is None:
                            # Get process info using psutil
                            try:
                                p = psutil.Process(proc.pid)
                                cpu = p.cpu_percent(interval=0.1)
                                mem = round(p.memory_info().rss / 1024 / 1024, 1)  # MB
                                
                                youos_processes.append({
                                    'name': app_name,
                                    'pid': proc.pid,
                                    'status': 'Running',
                                    'cpu': cpu,
                                    'memory': mem,
                                    'process': proc
                                })
                            except (psutil.NoSuchProcess, psutil.AccessDenied):
                                # Process exists in subprocess but not accessible via psutil
                                youos_processes.append({
                                    'name': app_name,
                                    'pid': proc.pid,
                                    'status': 'Running',
                                    'cpu': 0,
                                    'memory': 0,
                                    'process': proc
                                })
                    except:
                        continue
        
        return youos_processes
    
    def refresh_processes(self):
        """Refresh the process list"""
        try:
            # Get YouOS processes
            processes = self.get_youos_processes()
            
            # Store current selection
            selected_row = self.process_table.currentRow()
            
            # Clear table
            self.process_table.setRowCount(0)
            
            # Add processes to table
            for proc in processes:
                row = self.process_table.rowCount()
                self.process_table.insertRow(row)
                
                self.process_table.setItem(row, 0, QTableWidgetItem(proc['name']))
                self.process_table.setItem(row, 1, QTableWidgetItem(proc['status']))
                self.process_table.setItem(row, 2, QTableWidgetItem(str(proc['pid'])))
                self.process_table.setItem(row, 3, QTableWidgetItem(f"{proc['cpu']:.1f}"))
                self.process_table.setItem(row, 4, QTableWidgetItem(f"{proc['memory']:.1f}"))
                
                # Store process object in the first column
                self.process_table.item(row, 0).setData(Qt.ItemDataRole.UserRole, proc)
            
            # Restore selection if possible
            if selected_row >= 0 and selected_row < self.process_table.rowCount():
                self.process_table.selectRow(selected_row)
            
            # Update performance tab
            self.update_performance_info(processes)
            
            # Update status
            self.status_label.setText(f"Last updated: {QTimer().remainingTime()} | {len(processes)} YouOS processes")
            
            # Apply filter if search text exists
            if self.search_input.text():
                self.filter_processes()
                
        except Exception as e:
            self.status_label.setText(f"Error: {str(e)}")
    
    def filter_processes(self):
        """Filter processes based on search text"""
        search_text = self.search_input.text().lower()
        
        for row in range(self.process_table.rowCount()):
            item = self.process_table.item(row, 0)
            if item:
                should_show = search_text in item.text().lower()
                self.process_table.setRowHidden(row, not should_show)
    
    def update_performance_info(self, processes):
        """Update performance information"""
        try:
            # System CPU
            cpu_percent = psutil.cpu_percent(interval=None)
            self.cpu_label.setText(f"CPU Usage: {cpu_percent}%")
            
            # System Memory
            memory = psutil.virtual_memory()
            self.memory_label.setText(f"Memory Usage: {memory.percent}% ({round(memory.used / 1024 / 1024 / 1024, 1)} GB / {round(memory.total / 1024 / 1024 / 1024, 1)} GB)")
            
            # YouOS processes count
            self.processes_count_label.setText(f"YouOS Processes: {len(processes)}")
            
        except Exception as e:
            print(f"Error updating performance info: {e}")
    
    def end_selected_task(self):
        """End the selected task"""
        selected_row = self.process_table.currentRow()
        
        if selected_row < 0:
            QMessageBox.warning(self, "No Selection", "Please select a process to end.")
            return
        
        # Get process info
        item = self.process_table.item(selected_row, 0)
        proc_data = item.data(Qt.ItemDataRole.UserRole)
        
        if not proc_data:
            QMessageBox.warning(self, "Error", "Could not get process information.")
            return
        
        app_name = proc_data['name']
        pid = proc_data['pid']
        
        # Confirm
        reply = QMessageBox.question(
            self,
            "Confirm End Task",
            f"Are you sure you want to end '{app_name}' (PID: {pid})?\n\n"
            "This may cause data loss if the program has unsaved work.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            try:
                # Use desktop manager's close method
                if self.desktop_manager and hasattr(self.desktop_manager, 'close_app_instances'):
                    self.desktop_manager.close_app_instances(app_name)
                    self.status_label.setText(f"Ended task: {app_name}")
                else:
                    # Fallback: terminate the process directly
                    proc = proc_data['process']
                    proc.terminate()
                    try:
                        proc.wait(timeout=3)
                    except:
                        proc.kill()
                    self.status_label.setText(f"Terminated: {app_name}")
                
                # Refresh after a short delay
                QTimer.singleShot(500, self.refresh_processes)
                
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Failed to end task: {str(e)}")
    
    def closeEvent(self, event):
        """Handle window close"""
        self.refresh_timer.stop()
        event.accept()


def main():
    app = QApplication(sys.argv)
    task_manager = TaskManager()
    task_manager.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()