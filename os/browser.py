"""
Yousuf Browser - Full Web Browser with CSS, JS, and External Resources Support
Desktop-integrated browser application compatible with YouOS
"""

import sys
import os
import socketserver
import threading
from pathlib import Path
from PyQt6.QtGui import QIcon, QFont, QAction
from PyQt6.QtWidgets import (QApplication, QMainWindow, QToolBar, QLineEdit, 
                              QFileDialog, QTextEdit, QStatusBar, QDialog, 
                              QVBoxLayout, QHBoxLayout, QLabel, QCheckBox, 
                              QPushButton, QComboBox, QSystemTrayIcon, QMenu)
from PyQt6.QtWebEngineWidgets import QWebEngineView
from PyQt6.QtCore import QUrl, QSize
import http.server

class SimpleHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

class BrowserWindow(QMainWindow):
    def __init__(self, initial_file=None):
        super().__init__()
        self.server = None
        self.server_thread = None
        self.minimized_to_tray = False  # Track if we're in tray mode
        
        self.setWindowTitle("Yousuf Browser")
        self.setGeometry(100, 100, 1200, 800)
        
        # Try to set icon but don't fail if it doesn't exist
        icon_path = Path(__file__).parent / "assets" / "icons" / "browser.png"
        if not icon_path.exists():
            icon_path = Path(__file__).parent / "public" / "icon.jpg"
        
        if icon_path.exists():
            self.setWindowIcon(QIcon(str(icon_path)))
            
            # Setup system tray only if icon exists
            self.tray_icon = QSystemTrayIcon(self)
            self.tray_icon.setIcon(QIcon(str(icon_path)))
            
            tray_menu = QMenu()
            show_action = QAction("Show Browser", self)
            show_action.triggered.connect(self.restore_from_tray)
            tray_menu.addAction(show_action)
            
            exit_action = QAction("Exit", self)
            exit_action.triggered.connect(self.close_completely)
            tray_menu.addAction(exit_action)
            
            self.tray_icon.setContextMenu(tray_menu)
            self.tray_icon.activated.connect(self.tray_icon_activated)
        else:
            self.tray_icon = None
        
        self.start_local_server()
        
        self.browser = QWebEngineView()
        
        welcome_html = """
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
            <title>Welcome to Yousuf Browser</title>
            <style>
                * {
                    margin: 0;
                    padding: 0;
                    box-sizing: border-box;
                }
                body {
                    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
                    background: linear-gradient(135deg, #6366f1 0%, #8b5cf6 50%, #d946ef 100%);
                    color: white;
                    display: flex;
                    justify-content: center;
                    align-items: center;
                    height: 100vh;
                    overflow: hidden;
                }
                .container {
                    text-align: center;
                    background: rgba(255, 255, 255, 0.1);
                    padding: 80px 60px;
                    border-radius: 32px;
                    backdrop-filter: blur(20px);
                    box-shadow: 0 20px 60px rgba(0, 0, 0, 0.3);
                    border: 1px solid rgba(255, 255, 255, 0.2);
                    animation: fadeIn 0.8s ease-out;
                    max-width: 600px;
                }
                @keyframes fadeIn {
                    from { 
                        opacity: 0; 
                        transform: translateY(-30px) scale(0.95);
                    }
                    to { 
                        opacity: 1; 
                        transform: translateY(0) scale(1);
                    }
                }
                .logo {
                    font-size: 5em;
                    margin-bottom: 24px;
                    filter: drop-shadow(0 4px 12px rgba(0, 0, 0, 0.2));
                    animation: float 3s ease-in-out infinite;
                }
                @keyframes float {
                    0%, 100% { transform: translateY(0px); }
                    50% { transform: translateY(-10px); }
                }
                h1 {
                    font-size: 3.5em;
                    font-weight: 700;
                    margin-bottom: 16px;
                    background: linear-gradient(to right, #fff, #e0e7ff);
                    -webkit-background-clip: text;
                    -webkit-text-fill-color: transparent;
                    background-clip: text;
                }
                .subtitle {
                    font-size: 1.3em;
                    margin-bottom: 12px;
                    opacity: 0.95;
                    font-weight: 500;
                }
                .description {
                    font-size: 1.1em;
                    opacity: 0.85;
                    line-height: 1.6;
                }
                .features {
                    margin-top: 40px;
                    display: flex;
                    justify-content: center;
                    gap: 32px;
                }
                .feature {
                    font-size: 0.95em;
                    opacity: 0.9;
                    display: flex;
                    align-items: center;
                    gap: 8px;
                }
                .feature-icon {
                    font-size: 1.4em;
                }
            </style>
            <script>
                console.log('🌐 Yousuf Browser initialized successfully');
                console.log('✅ CSS rendering enabled');
                console.log('✅ JavaScript execution enabled');
                console.log('✅ External resources supported');
            </script>
        </head>
        <body>
            <div class="container">
                <div class="logo">🌐</div>
                <h1>Yousuf Browser</h1>
                <p class="subtitle">Modern Web Browsing Experience</p>
                <p class="description">Full CSS & JavaScript support with seamless desktop integration</p>
                <div class="features">
                    <div class="feature">
                        <span class="feature-icon">⚡</span>
                        <span>Fast</span>
                    </div>
                    <div class="feature">
                        <span class="feature-icon">🔒</span>
                        <span>Secure</span>
                    </div>
                    <div class="feature">
                        <span class="feature-icon">🎨</span>
                        <span>Beautiful</span>
                    </div>
                </div>
            </div>
        </body>
        </html>
        """
        
        if initial_file and os.path.exists(initial_file):
            file_dir = os.path.dirname(os.path.abspath(initial_file))
            file_name = os.path.basename(initial_file)
            os.chdir(file_dir)
            url = f"http://localhost:8765/{file_name}"
            self.browser.setUrl(QUrl(url))
        else:
            self.browser.setHtml(welcome_html)
        
        self.browser.loadFinished.connect(self.on_load_finished)
        self.browser.loadProgress.connect(self.on_load_progress)
        
        self.url_bar = QLineEdit()
        self.url_bar.setPlaceholderText("Enter URL or search...")
        self.url_bar.setStyleSheet("""
            QLineEdit {
                padding: 8px 16px;
                border-radius: 8px;
                border: 1px solid #d1d5db;
                font-size: 14px;
                background: white;
                color: #1f2937;
            }
            QLineEdit:focus {
                border: 2px solid #6366f1;
                background: #fafafa;
            }
        """)
        self.url_bar.returnPressed.connect(self.navigate_to_url)
        self.browser.urlChanged.connect(self.update_url_bar)
        
        self.create_toolbar()
        
        self.settings = {
            "homepage": "home",
            "show_status_bar": True,
            "dark_mode": False
        }
        
        self.status = QStatusBar()
        self.status.setStyleSheet("""
            QStatusBar {
                background: #f9fafb;
                color: #4b5563;
                border-top: 1px solid #e5e7eb;
                padding: 4px;
                font-size: 12px;
            }
        """)
        self.setStatusBar(self.status)
        self.status.showMessage("Ready")
        
        self.setCentralWidget(self.browser)
        
        # Fixed styling to ensure proper visibility
        self.setStyleSheet("""
            QMainWindow {
                background: #ffffff;
            }
            QToolBar {
                background: #f9fafb;
                border-bottom: 1px solid #e5e7eb;
                spacing: 8px;
                padding: 8px;
            }
            QToolBar QToolButton {
                border-radius: 6px;
                padding: 6px 12px;
                margin: 2px;
                background: white;
                border: 1px solid #e5e7eb;
                color: #1f2937;
                font-size: 13px;
            }
            QToolBar QToolButton:hover {
                background: #f3f4f6;
                border-color: #d1d5db;
            }
            QToolBar QToolButton:pressed {
                background: #e5e7eb;
            }
            QMenu {
                background: white;
                border: 1px solid #e5e7eb;
                border-radius: 6px;
                padding: 4px;
            }
            QMenu::item {
                padding: 6px 20px;
                color: #1f2937;
                border-radius: 4px;
            }
            QMenu::item:selected {
                background: #f3f4f6;
            }
        """)
    
    def start_local_server(self):
        """Start local HTTP server for CORS-free file loading"""
        try:
            self.server = socketserver.TCPServer(("", 8765), SimpleHTTPRequestHandler)
            self.server_thread = threading.Thread(target=self.server.serve_forever, daemon=True)
            self.server_thread.start()
        except:
            pass
    
    def tray_icon_activated(self, reason):
        """Handle tray icon click"""
        if reason == QSystemTrayIcon.ActivationReason.Trigger:
            self.restore_from_tray()
    
    def restore_from_tray(self):
        """Restore window from tray"""
        self.show()
        self.activateWindow()
        self.raise_()
        self.minimized_to_tray = False
        if self.tray_icon:
            self.tray_icon.hide()
    
    def close_completely(self):
        """Completely close the browser"""
        if self.tray_icon:
            self.tray_icon.hide()
        if self.server:
            self.server.shutdown()
            self.server.server_close()
        QApplication.quit()
    
    def closeEvent(self, event):
        """Handle window close - minimize to tray if available, otherwise close"""
        # Check if we should minimize to tray (only if Ctrl is held)
        modifiers = QApplication.keyboardModifiers()
        
        if self.tray_icon and False:  # Disabled tray minimization for YouOS integration
            # Minimize to tray
            self.hide()
            self.minimized_to_tray = True
            self.tray_icon.show()
            self.tray_icon.showMessage(
                "Yousuf Browser",
                "Browser is still running in the system tray.",
                QSystemTrayIcon.MessageIcon.Information,
                2000
            )
            event.ignore()
        else:
            # Actually close the application
            if self.tray_icon:
                self.tray_icon.hide()
            if self.server:
                self.server.shutdown()
                self.server.server_close()
            event.accept()
    
    def create_toolbar(self):
        toolbar = QToolBar("Navigation")
        toolbar.setMovable(False)
        toolbar.setIconSize(QSize(16, 16))
        self.addToolBar(toolbar)
        
        back_btn = QAction("← Back", self)
        back_btn.setStatusTip("Go back to previous page")
        back_btn.triggered.connect(self.browser.back)
        toolbar.addAction(back_btn)
        
        forward_btn = QAction("Forward →", self)
        forward_btn.setStatusTip("Go forward to next page")
        forward_btn.triggered.connect(self.browser.forward)
        toolbar.addAction(forward_btn)
        
        reload_btn = QAction("⟳ Reload", self)
        reload_btn.setStatusTip("Reload current page")
        reload_btn.triggered.connect(self.browser.reload)
        toolbar.addAction(reload_btn)
        
        home_btn = QAction("⌂ Home", self)
        home_btn.setStatusTip("Go to home page")
        home_btn.triggered.connect(self.navigate_home)
        toolbar.addAction(home_btn)
        
        toolbar.addSeparator()
        
        open_file_btn = QAction("📁 Open File", self)
        open_file_btn.setStatusTip("Open HTML file from disk")
        open_file_btn.triggered.connect(self.open_file)
        toolbar.addAction(open_file_btn)
        
        toolbar.addSeparator()
        toolbar.addWidget(self.url_bar)
        toolbar.addSeparator()
        
        view_source_btn = QAction("</> Source", self)
        view_source_btn.setStatusTip("View page source code")
        view_source_btn.triggered.connect(self.view_source)
        toolbar.addAction(view_source_btn)
        
        zoom_in_btn = QAction("+ Zoom", self)
        zoom_in_btn.setStatusTip("Zoom in")
        zoom_in_btn.triggered.connect(lambda: self.browser.setZoomFactor(self.browser.zoomFactor() + 0.1))
        toolbar.addAction(zoom_in_btn)
        
        zoom_out_btn = QAction("− Zoom", self)
        zoom_out_btn.setStatusTip("Zoom out")
        zoom_out_btn.triggered.connect(lambda: self.browser.setZoomFactor(self.browser.zoomFactor() - 0.1))
        toolbar.addAction(zoom_out_btn)
        
        toolbar.addSeparator()
        
        settings_btn = QAction("⚙ Settings", self)
        settings_btn.setStatusTip("Open browser settings")
        settings_btn.triggered.connect(self.show_settings)
        toolbar.addAction(settings_btn)
    
    def show_settings(self):
        settings_dialog = QDialog(self)
        settings_dialog.setWindowTitle("Settings")
        settings_dialog.setFixedWidth(400)
        settings_dialog.setStyleSheet("""
            QDialog {
                background: white;
            }
            QLabel {
                color: #1f2937;
                font-size: 13px;
            }
            QCheckBox {
                color: #1f2937;
                font-size: 13px;
            }
            QComboBox {
                background: white;
                color: #1f2937;
                border: 1px solid #d1d5db;
                border-radius: 4px;
                padding: 6px;
            }
            QPushButton {
                background-color: #6366f1;
                color: white;
                padding: 8px;
                border-radius: 4px;
                border: none;
                font-size: 13px;
            }
            QPushButton:hover {
                background-color: #4f46e5;
            }
        """)
        
        layout = QVBoxLayout()
        
        # Homepage Setting
        hp_layout = QHBoxLayout()
        hp_label = QLabel("Homepage:")
        hp_combo = QComboBox()
        hp_combo.addItems(["Default", "Blank", "Google"])
        hp_layout.addWidget(hp_label)
        hp_layout.addWidget(hp_combo)
        layout.addLayout(hp_layout)
        
        # Status Bar Setting
        status_bar_cb = QCheckBox("Show Status Bar")
        status_bar_cb.setChecked(self.settings["show_status_bar"])
        layout.addWidget(status_bar_cb)
        
        # Save Button
        save_btn = QPushButton("Save Settings")
        save_btn.clicked.connect(lambda: self.save_settings(settings_dialog, hp_combo.currentText(), status_bar_cb.isChecked()))
        layout.addWidget(save_btn)
        
        settings_dialog.setLayout(layout)
        settings_dialog.exec()

    def save_settings(self, dialog, hp, status):
        self.settings["homepage"] = hp
        self.settings["show_status_bar"] = status
        
        if status:
            self.status.show()
        else:
            self.status.hide()
            
        dialog.accept()
        self.status.showMessage("Settings saved", 3000)
    
    def navigate_home(self):
        welcome_html = """
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
            <title>Welcome to Yousuf Browser</title>
            <style>
                * {
                    margin: 0;
                    padding: 0;
                    box-sizing: border-box;
                }
                body {
                    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
                    background: linear-gradient(135deg, #6366f1 0%, #8b5cf6 50%, #d946ef 100%);
                    color: white;
                    display: flex;
                    justify-content: center;
                    align-items: center;
                    height: 100vh;
                    overflow: hidden;
                }
                .container {
                    text-align: center;
                    background: rgba(255, 255, 255, 0.1);
                    padding: 80px 60px;
                    border-radius: 32px;
                    backdrop-filter: blur(20px);
                    box-shadow: 0 20px 60px rgba(0, 0, 0, 0.3);
                    border: 1px solid rgba(255, 255, 255, 0.2);
                    animation: fadeIn 0.8s ease-out;
                    max-width: 600px;
                }
                @keyframes fadeIn {
                    from { 
                        opacity: 0; 
                        transform: translateY(-30px) scale(0.95);
                    }
                    to { 
                        opacity: 1; 
                        transform: translateY(0) scale(1);
                    }
                }
                .logo {
                    font-size: 5em;
                    margin-bottom: 24px;
                    filter: drop-shadow(0 4px 12px rgba(0, 0, 0, 0.2));
                    animation: float 3s ease-in-out infinite;
                }
                @keyframes float {
                    0%, 100% { transform: translateY(0px); }
                    50% { transform: translateY(-10px); }
                }
                h1 {
                    font-size: 3.5em;
                    font-weight: 700;
                    margin-bottom: 16px;
                    background: linear-gradient(to right, #fff, #e0e7ff);
                    -webkit-background-clip: text;
                    -webkit-text-fill-color: transparent;
                    background-clip: text;
                }
                .subtitle {
                    font-size: 1.3em;
                    margin-bottom: 12px;
                    opacity: 0.95;
                    font-weight: 500;
                }
                .description {
                    font-size: 1.1em;
                    opacity: 0.85;
                    line-height: 1.6;
                }
                .features {
                    margin-top: 40px;
                    display: flex;
                    justify-content: center;
                    gap: 32px;
                }
                .feature {
                    font-size: 0.95em;
                    opacity: 0.9;
                    display: flex;
                    align-items: center;
                    gap: 8px;
                }
                .feature-icon {
                    font-size: 1.4em;
                }
            </style>
        </head>
        <body>
            <div class="container">
                <div class="logo">🌐</div>
                <h1>Yousuf Browser</h1>
                <p class="subtitle">Modern Web Browsing Experience</p>
                <p class="description">Full CSS & JavaScript support with seamless desktop integration</p>
                <div class="features">
                    <div class="feature">
                        <span class="feature-icon">⚡</span>
                        <span>Fast</span>
                    </div>
                    <div class="feature">
                        <span class="feature-icon">🔒</span>
                        <span>Secure</span>
                    </div>
                    <div class="feature">
                        <span class="feature-icon">🎨</span>
                        <span>Beautiful</span>
                    </div>
                </div>
            </div>
        </body>
        </html>
        """
        self.browser.setHtml(welcome_html)
    
    def navigate_to_url(self):
        url = self.url_bar.text()
        if url:
            if not url.startswith('http://') and not url.startswith('https://') and not url.startswith('file://'):
                url = 'http://' + url
            self.browser.setUrl(QUrl(url))
    
    def update_url_bar(self, url):
        self.url_bar.setText(url.toString())
        self.url_bar.setCursorPosition(0)
    
    def open_file(self):
        filename, _ = QFileDialog.getOpenFileName(
            self, 
            "Open HTML File", 
            "", 
            "HTML Files (*.html *.htm);;All Files (*.*)"
        )
        
        if filename:
            file_dir = os.path.dirname(os.path.abspath(filename))
            file_name = os.path.basename(filename)
            os.chdir(file_dir)
            url = f"http://localhost:8765/{file_name}"
            self.browser.setUrl(QUrl(url))
            self.status.showMessage(f"Opened: {filename}")
    
    def view_source(self):
        self.browser.page().toHtml(self.show_source_code)
    
    def show_source_code(self, html):
        source_window = QMainWindow(self)
        source_window.setWindowTitle("Page Source - Yousuf Browser")
        source_window.setGeometry(150, 150, 1000, 600)
        
        text_edit = QTextEdit()
        text_edit.setPlainText(html)
        text_edit.setReadOnly(True)
        text_edit.setStyleSheet("""
            QTextEdit {
                font-family: 'Courier New', monospace;
                font-size: 12px;
                background: #282a36;
                color: #f8f8f2;
                padding: 12px;
            }
        """)
        
        source_window.setCentralWidget(text_edit)
        source_window.show()
    
    def on_load_finished(self, success):
        if success:
            self.status.showMessage("Page loaded successfully", 3000)
        else:
            self.status.showMessage("Failed to load page", 3000)
    
    def on_load_progress(self, progress):
        self.status.showMessage(f"Loading... {progress}%")


def main():
    """Main entry point for standalone browser"""
    app = QApplication(sys.argv)
    app.setApplicationName("Yousuf Browser")
    
    initial_file = sys.argv[1] if len(sys.argv) > 1 else None
    window = BrowserWindow(initial_file)
    window.show()
    
    sys.exit(app.exec())


if __name__ == "__main__":
    main()