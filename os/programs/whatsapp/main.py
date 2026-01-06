"""
YouOS WhatsApp Desktop Client
A full-featured WhatsApp Web client with persistent login
"""

import sys
import os
from pathlib import Path
from PyQt6.QtWidgets import (QApplication, QMainWindow, QVBoxLayout, QHBoxLayout,
                              QWidget, QPushButton, QLabel, QFrame, QMessageBox,
                              QMenu)
from PyQt6.QtCore import Qt, QUrl, QTimer, pyqtSignal
from PyQt6.QtGui import QAction, QIcon
from PyQt6.QtWebEngineWidgets import QWebEngineView
from PyQt6.QtWebEngineCore import QWebEngineProfile, QWebEnginePage

# Colors matching YouOS theme
COLORS = {
    'bg_primary': '#0f0f1e',
    'bg_secondary': '#1a1a2e',
    'bg_tertiary': '#252538',
    'accent_primary': '#3b82f6',
    'accent_hover': '#60a5fa',
    'text_primary': '#ffffff',
    'text_secondary': '#9ca3af',
    'border': '#374151',
    'success': '#10b981',
    'error': '#ef4444',
}

# Data directory for persistent storage
DATA_DIR = Path.home() / '.youos' / 'whatsapp'
DATA_DIR.mkdir(parents=True, exist_ok=True)


class GlassFrame(QFrame):
    """Glassmorphic frame widget"""
    def __init__(self, parent=None, opacity=0.15):
        super().__init__(parent)
        self.setStyleSheet(f"""
            QFrame {{
                background: rgba(255, 255, 255, {opacity});
                border: 1px solid rgba(255, 255, 255, 0.2);
                border-radius: 12px;
            }}
        """)


class WhatsAppWebView(QWebEngineView):
    """Custom web view for WhatsApp Web"""
    
    message_received = pyqtSignal(str, str)  # title, message
    
    def __init__(self, parent=None):
        super().__init__(parent)
        
        # Create persistent profile
        profile = QWebEngineProfile("whatsapp", self)
        profile.setPersistentStoragePath(str(DATA_DIR / "storage"))
        profile.setCachePath(str(DATA_DIR / "cache"))
        profile.setPersistentCookiesPolicy(
            QWebEngineProfile.PersistentCookiesPolicy.ForcePersistentCookies
        )
        
        # Create page with the profile
        page = QWebEnginePage(profile, self)
        self.setPage(page)
        
        # Set user agent to avoid mobile version
        profile.setHttpUserAgent(
            "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
        )
        
        # Load WhatsApp Web
        self.setUrl(QUrl("https://web.whatsapp.com"))
        
        # Enable features
        settings = self.settings()
        settings.setAttribute(settings.WebAttribute.LocalStorageEnabled, True)
        settings.setAttribute(settings.WebAttribute.JavascriptEnabled, True)
        settings.setAttribute(settings.WebAttribute.JavascriptCanOpenWindows, True)
        settings.setAttribute(settings.WebAttribute.AllowWindowActivationFromJavaScript, True)
        settings.setAttribute(settings.WebAttribute.LocalContentCanAccessRemoteUrls, True)
        settings.setAttribute(settings.WebAttribute.PluginsEnabled, True)
        
        # Set up notification monitoring
        self.notification_timer = QTimer()
        self.notification_timer.timeout.connect(self.check_for_notifications)
        self.notification_timer.start(2000)  # Check every 2 seconds
        
        self.last_message_count = 0
    
    def check_for_notifications(self):
        """Check for new messages and send notifications"""
        # JavaScript to check for unread messages
        js_code = """
        (function() {
            try {
                // Check for unread message count
                const unreadElements = document.querySelectorAll('[data-icon="unread-count"], .OUeyt, ._1pJ9J, [aria-label*="unread"]');
                let totalUnread = 0;
                
                unreadElements.forEach(el => {
                    const text = el.textContent || el.innerText;
                    const num = parseInt(text);
                    if (!isNaN(num)) {
                        totalUnread += num;
                    }
                });
                
                // Also check for notification badges
                const badges = document.querySelectorAll('._1pJ9J, .OUeyt');
                if (badges.length > 0) {
                    totalUnread = Math.max(totalUnread, badges.length);
                }
                
                // Get latest chat name if available
                let latestChat = 'WhatsApp';
                const chatElements = document.querySelectorAll('[data-testid="cell-frame-title"]');
                if (chatElements.length > 0) {
                    latestChat = chatElements[0].textContent || 'WhatsApp';
                }
                
                return {
                    unreadCount: totalUnread,
                    latestChat: latestChat
                };
            } catch (e) {
                return { unreadCount: 0, latestChat: 'WhatsApp' };
            }
        })();
        """
        
        self.page().runJavaScript(js_code, self.handle_notification_check)
    
    def handle_notification_check(self, result):
        """Handle the result of notification check"""
        if result and isinstance(result, dict):
            unread_count = result.get('unreadCount', 0)
            latest_chat = result.get('latestChat', 'WhatsApp')
            
            if unread_count > self.last_message_count:
                # New message received
                if unread_count == 1:
                    message = f"New message from {latest_chat}"
                else:
                    message = f"{unread_count} new messages"
                
                self.message_received.emit("WhatsApp", message)
            
            self.last_message_count = unread_count


class WhatsAppWindow(QMainWindow):
    """Main WhatsApp window"""
    
    notification_requested = pyqtSignal(str, str, str)  # title, message, icon
    
    def __init__(self):
        super().__init__()
        self.setWindowTitle("💬 WhatsApp - YouOS")
        self.setMinimumSize(1000, 700)
        self.resize(1200, 800)
        
        # Set window icon
        self.setWindowIcon(QIcon.fromTheme("whatsapp"))
        
        self.setup_ui()
        self.apply_styles()
        
        # Connection status timer
        self.status_timer = QTimer(self)
        self.status_timer.timeout.connect(self.update_status)
        self.status_timer.start(2000)
        
        # Connect notification signal
        self.web_view.message_received.connect(self.on_message_received)
    
    def on_message_received(self, title, message):
        """Handle new message notification"""
        self.notification_requested.emit(title, message, "💬")
        
        # Try to send notification to desktop if running in desktop mode
        try:
            # Check if we're running as a separate process
            import sys
            if hasattr(sys, '_desktop_manager'):
                sys._desktop_manager.add_notification(title, message, "💬", "Now")
            else:
                # Send notification via D-Bus or other system notification
                try:
                    import subprocess
                    subprocess.run([
                        'notify-send', 
                        '--icon=whatsapp',
                        title, 
                        message
                    ], capture_output=True, timeout=2)
                except:
                    pass
        except Exception as e:
            print(f"Could not send notification: {e}")
    
    def setup_ui(self):
        """Setup the user interface"""
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        layout = QVBoxLayout(central_widget)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        
        # Create web view first (needed by top bar)
        self.web_view = WhatsAppWebView()
        self.web_view.loadStarted.connect(self.on_load_started)
        self.web_view.loadFinished.connect(self.on_load_finished)
        
        # Top bar (needs web_view to exist)
        top_bar = self.create_top_bar()
        layout.addWidget(top_bar)
        
        # Add web view to layout
        layout.addWidget(self.web_view)
        
        # Status bar
        status_bar = self.create_status_bar()
        layout.addWidget(status_bar)
    
    def create_top_bar(self):
        """Create the top navigation bar"""
        top_bar = GlassFrame()
        top_bar.setFixedHeight(60)
        
        layout = QHBoxLayout(top_bar)
        layout.setContentsMargins(15, 10, 15, 10)
        layout.setSpacing(10)
        
        # Logo and title
        title_layout = QHBoxLayout()
        icon_label = QLabel("💬")
        icon_label.setStyleSheet("font-size: 28px;")
        title_layout.addWidget(icon_label)
        
        title_label = QLabel("WhatsApp")
        title_label.setStyleSheet(f"""
            color: {COLORS['text_primary']};
            font-size: 18px;
            font-weight: bold;
        """)
        title_layout.addWidget(title_label)
        layout.addLayout(title_layout)
        
        layout.addStretch()
        
        # Navigation buttons
        self.back_btn = QPushButton("◀")
        self.back_btn.setFixedSize(40, 40)
        self.back_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.back_btn.clicked.connect(self.web_view.back)
        self.back_btn.setEnabled(False)
        layout.addWidget(self.back_btn)
        
        self.forward_btn = QPushButton("▶")
        self.forward_btn.setFixedSize(40, 40)
        self.forward_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.forward_btn.clicked.connect(self.web_view.forward)
        self.forward_btn.setEnabled(False)
        layout.addWidget(self.forward_btn)
        
        self.reload_btn = QPushButton("🔄")
        self.reload_btn.setFixedSize(40, 40)
        self.reload_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.reload_btn.clicked.connect(self.web_view.reload)
        layout.addWidget(self.reload_btn)
        
        # Menu button
        menu_btn = QPushButton("⋮")
        menu_btn.setFixedSize(40, 40)
        menu_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        menu_btn.clicked.connect(self.show_menu)
        layout.addWidget(menu_btn)
        
        return top_bar
    
    def create_status_bar(self):
        """Create the status bar"""
        status_bar = QFrame()
        status_bar.setFixedHeight(35)
        status_bar.setStyleSheet(f"background: {COLORS['bg_secondary']};")
        
        layout = QHBoxLayout(status_bar)
        layout.setContentsMargins(15, 5, 15, 5)
        
        self.status_icon = QLabel("🔵")
        self.status_icon.setStyleSheet("font-size: 14px;")
        layout.addWidget(self.status_icon)
        
        self.status_label = QLabel("Connecting...")
        self.status_label.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 12px;
        """)
        layout.addWidget(self.status_label)
        
        layout.addStretch()
        
        self.connection_label = QLabel("")
        self.connection_label.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 11px;
        """)
        layout.addWidget(self.connection_label)
        
        return status_bar
    
    def show_menu(self):
        """Show the context menu"""
        menu = QMenu(self)
        menu.setStyleSheet(f"""
            QMenu {{
                background: {COLORS['bg_secondary']};
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                padding: 5px;
            }}
            QMenu::item {{
                color: {COLORS['text_primary']};
                padding: 8px 20px;
                border-radius: 4px;
            }}
            QMenu::item:selected {{
                background: {COLORS['accent_primary']};
            }}
        """)
        
        # Reload action
        reload_action = QAction("🔄 Reload", self)
        reload_action.triggered.connect(self.web_view.reload)
        menu.addAction(reload_action)
        
        # Hard reload action
        hard_reload_action = QAction("🔄 Hard Reload", self)
        hard_reload_action.triggered.connect(self.hard_reload)
        menu.addAction(hard_reload_action)
        
        menu.addSeparator()
        
        # Logout action
        logout_action = QAction("🚪 Logout", self)
        logout_action.triggered.connect(self.logout)
        menu.addAction(logout_action)
        
        # Clear data action
        clear_action = QAction("🗑️ Clear Data", self)
        clear_action.triggered.connect(self.clear_data)
        menu.addAction(clear_action)
        
        menu.addSeparator()
        
        # About action
        about_action = QAction("ℹ️ About", self)
        about_action.triggered.connect(self.show_about)
        menu.addAction(about_action)
        
        # Show menu at button position
        menu.exec(self.sender().mapToGlobal(self.sender().rect().bottomLeft()))
    
    def hard_reload(self):
        """Hard reload the page"""
        self.web_view.page().profile().clearHttpCache()
        self.web_view.reload()
        self.status_label.setText("Reloading...")
    
    def logout(self):
        """Logout from WhatsApp"""
        reply = QMessageBox.question(
            self,
            "Logout",
            "Are you sure you want to logout from WhatsApp?\n\n"
            "You'll need to scan the QR code again to login.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            # Clear cookies and storage
            self.web_view.page().profile().cookieStore().deleteAllCookies()
            
            # Execute JavaScript to logout
            self.web_view.page().runJavaScript("""
                localStorage.clear();
                sessionStorage.clear();
                indexedDB.databases().then(dbs => {
                    dbs.forEach(db => indexedDB.deleteDatabase(db.name));
                });
            """)
            
            # Reload page
            self.web_view.reload()
            self.status_label.setText("Logged out")
    
    def clear_data(self):
        """Clear all app data"""
        reply = QMessageBox.question(
            self,
            "Clear Data",
            "This will delete all WhatsApp data including:\n"
            "• Login session\n"
            "• Cached media\n"
            "• Local storage\n\n"
            "Continue?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            try:
                # Clear profile data
                profile = self.web_view.page().profile()
                profile.clearHttpCache()
                profile.cookieStore().deleteAllCookies()
                
                # Clear local storage
                self.web_view.page().runJavaScript("""
                    localStorage.clear();
                    sessionStorage.clear();
                    indexedDB.databases().then(dbs => {
                        dbs.forEach(db => indexedDB.deleteDatabase(db.name));
                    });
                """)
                
                QMessageBox.information(
                    self,
                    "Data Cleared",
                    "All WhatsApp data has been cleared.\n\n"
                    "The page will now reload."
                )
                
                self.web_view.reload()
                
            except Exception as e:
                QMessageBox.critical(
                    self,
                    "Error",
                    f"Failed to clear data: {str(e)}"
                )
    
    def show_about(self):
        """Show about dialog"""
        QMessageBox.about(
            self,
            "About WhatsApp for YouOS",
            "WhatsApp Desktop Client\n\n"
            "A full-featured WhatsApp Web client with persistent login.\n\n"
            "Features:\n"
            "• Full WhatsApp Web functionality\n"
            "• Persistent login sessions\n"
            "• Media support\n"
            "• Notifications\n\n"
            "Powered by PyQt6 WebEngine\n"
            "Part of YouOS 10"
        )
    
    def on_load_started(self):
        """Handle page load start"""
        self.status_label.setText("Loading WhatsApp...")
        self.reload_btn.setEnabled(False)
    
    def on_load_finished(self, success):
        """Handle page load finish"""
        if success:
            self.status_label.setText("Connected")
            self.status_icon.setText("🟢")
        else:
            self.status_label.setText("Failed to load")
            self.status_icon.setText("🔴")
        
        self.reload_btn.setEnabled(True)
        
        # Update navigation buttons
        self.back_btn.setEnabled(self.web_view.history().canGoBack())
        self.forward_btn.setEnabled(self.web_view.history().canGoForward())
    
    def update_status(self):
        """Update connection status"""
        # Check if page is loaded
        if self.web_view.url().toString() == "https://web.whatsapp.com/":
            # Try to detect if logged in via JavaScript
            self.web_view.page().runJavaScript(
                "document.querySelector('[data-icon=\"chat\"]') !== null",
                self.check_login_status
            )
    
    def check_login_status(self, is_logged_in):
        """Check if user is logged in"""
        if is_logged_in:
            self.status_icon.setText("🟢")
            self.connection_label.setText("Online")
        else:
            self.status_icon.setText("🟡")
            self.connection_label.setText("Waiting for login")
    
    def apply_styles(self):
        """Apply stylesheet"""
        self.setStyleSheet(f"""
            QMainWindow {{
                background: {COLORS['bg_primary']};
            }}
            QPushButton {{
                background: rgba(255, 255, 255, 0.1);
                border: 1px solid rgba(255, 255, 255, 0.2);
                border-radius: 8px;
                color: {COLORS['text_primary']};
                font-size: 14px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_primary']};
            }}
            QPushButton:disabled {{
                background: rgba(255, 255, 255, 0.05);
                color: {COLORS['text_secondary']};
            }}
        """)
    
    def closeEvent(self, event):
        """Handle window close"""
        # Save window geometry
        settings_file = DATA_DIR / "settings.json"
        try:
            import json
            settings = {
                'geometry': {
                    'x': self.x(),
                    'y': self.y(),
                    'width': self.width(),
                    'height': self.height()
                }
            }
            with open(settings_file, 'w') as f:
                json.dump(settings, f, indent=4)
        except:
            pass
        
        event.accept()


def main():
    """Main entry point"""
    app = QApplication(sys.argv)
    app.setApplicationName("WhatsApp")
    app.setOrganizationName("YouOS")
    
    window = WhatsAppWindow()
    
    # Restore window geometry if saved
    try:
        import json
        settings_file = DATA_DIR / "settings.json"
        if settings_file.exists():
            with open(settings_file, 'r') as f:
                settings = json.load(f)
                geom = settings.get('geometry', {})
                if all(k in geom for k in ['x', 'y', 'width', 'height']):
                    window.setGeometry(
                        geom['x'], geom['y'],
                        geom['width'], geom['height']
                    )
    except:
        pass
    
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()