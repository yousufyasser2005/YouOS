"""
Yousuf Browser - Full Web Browser with CSS, JS, and External Resources Support
Runs in a separate process to avoid conflicts with tkinter

Requirements: PyQt5, PyQtWebEngine (already installed)
"""

import sys
import os
import subprocess
import tempfile
import tkinter as tk
from tkinter import filedialog, messagebox

# كود المتصفح الذي سيعمل في عملية منفصلة
BROWSER_CODE = '''
import sys
import os
import http.server
import socketserver
import threading
import warnings

# تجاهل التحذيرات
warnings.filterwarnings("ignore")
os.environ["QT_LOGGING_RULES"] = "*=false"

from PyQt5.QtCore import QUrl
from PyQt5.QtWidgets import QApplication, QMainWindow, QToolBar, QAction, QLineEdit, QFileDialog, QTextEdit
from PyQt5.QtWebEngineWidgets import QWebEngineView

class SimpleHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        pass  # تعطيل رسائل السجل

class YousufBrowserWindow(QMainWindow):
    def __init__(self, initial_file=None):
        super().__init__()
        self.server = None
        self.server_thread = None
        self.setWindowTitle("Yousuf Browser")
        self.setGeometry(100, 100, 1200, 800)
        
        # بدء خادم محلي لتجنب مشاكل CORS
        self.start_local_server()
        
        self.browser = QWebEngineView()
        
        welcome_html = """
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
            <style>
                body {
                    font-family: Arial, sans-serif;
                    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
                    color: white;
                    display: flex;
                    justify-content: center;
                    align-items: center;
                    height: 100vh;
                    margin: 0;
                }
                .container {
                    text-align: center;
                    background: rgba(255, 255, 255, 0.15);
                    padding: 60px;
                    border-radius: 25px;
                    backdrop-filter: blur(10px);
                    box-shadow: 0 8px 32px 0 rgba(31, 38, 135, 0.37);
                    animation: fadeIn 1s;
                }
                h1 { font-size: 3em; margin-bottom: 20px; }
                p { font-size: 1.2em; line-height: 1.6; margin: 10px 0; }
                .emoji { font-size: 4em; margin-bottom: 20px; }
                @keyframes fadeIn {
                    from { opacity: 0; transform: translateY(-20px); }
                    to { opacity: 1; transform: translateY(0); }
                }
            </style>
            <script>
                console.log('Welcome to Yousuf Browser!');
                setTimeout(() => console.log('JavaScript is fully functional!'), 500);
            </script>
        </head>
        <body>
            <div class="container">
                <div class="emoji">🌐</div>
                <h1>Yousuf Browser</h1>
                <p>Full Web Browser with Complete CSS & JavaScript Support</p>
                <p>Open HTML files or enter URLs to browse</p>
            </div>
        </body>
        </html>
        """
        
        if initial_file and os.path.exists(initial_file):
            # استخدام الخادم المحلي لتجنب CORS
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
        self.url_bar.returnPressed.connect(self.navigate_to_url)
        self.browser.urlChanged.connect(self.update_url_bar)
        
        self.create_toolbar()
        
        from PyQt5.QtWidgets import QStatusBar
        self.status = QStatusBar()
        self.setStatusBar(self.status)
        self.status.showMessage("Ready")
        
        self.setCentralWidget(self.browser)
    
    def start_local_server(self):
        """بدء خادم HTTP محلي"""
        try:
            self.server = socketserver.TCPServer(("", 8765), SimpleHTTPRequestHandler)
            self.server_thread = threading.Thread(target=self.server.serve_forever, daemon=True)
            self.server_thread.start()
        except:
            pass  # إذا كان المنفذ مستخدماً، استمر بدون خادم
    
    def closeEvent(self, event):
        """عند إغلاق النافذة"""
        if self.server:
            self.server.shutdown()
            self.server.server_close()
        event.accept()
        QApplication.quit()  # إنهاء التطبيق بالكامل
    
    def create_toolbar(self):
        toolbar = QToolBar("Main Toolbar")
        toolbar.setMovable(False)
        self.addToolBar(toolbar)
        
        back_btn = QAction("⬅ Back", self)
        back_btn.triggered.connect(self.browser.back)
        toolbar.addAction(back_btn)
        
        forward_btn = QAction("➡ Forward", self)
        forward_btn.triggered.connect(self.browser.forward)
        toolbar.addAction(forward_btn)
        
        reload_btn = QAction("🔄 Reload", self)
        reload_btn.triggered.connect(self.browser.reload)
        toolbar.addAction(reload_btn)
        
        home_btn = QAction("🏠 Home", self)
        home_btn.triggered.connect(self.navigate_home)
        toolbar.addAction(home_btn)
        
        toolbar.addSeparator()
        
        open_file_btn = QAction("📂 Open File", self)
        open_file_btn.triggered.connect(self.open_file)
        toolbar.addAction(open_file_btn)
        
        toolbar.addSeparator()
        toolbar.addWidget(self.url_bar)
        toolbar.addSeparator()
        
        view_source_btn = QAction("</> Source", self)
        view_source_btn.triggered.connect(self.view_source)
        toolbar.addAction(view_source_btn)
        
        zoom_in_btn = QAction("🔍+", self)
        zoom_in_btn.triggered.connect(lambda: self.browser.setZoomFactor(self.browser.zoomFactor() + 0.1))
        toolbar.addAction(zoom_in_btn)
        
        zoom_out_btn = QAction("🔍-", self)
        zoom_out_btn.triggered.connect(lambda: self.browser.setZoomFactor(self.browser.zoomFactor() - 0.1))
        toolbar.addAction(zoom_out_btn)
    
    def navigate_home(self):
        welcome_html = """
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
            <style>
                body {
                    font-family: Arial, sans-serif;
                    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
                    color: white;
                    display: flex;
                    justify-content: center;
                    align-items: center;
                    height: 100vh;
                    margin: 0;
                }
                .container {
                    text-align: center;
                    background: rgba(255, 255, 255, 0.15);
                    padding: 60px;
                    border-radius: 25px;
                    backdrop-filter: blur(10px);
                }
                h1 { font-size: 3em; }
                .emoji { font-size: 4em; }
            </style>
        </head>
        <body>
            <div class="container">
                <div class="emoji">🌐</div>
                <h1>Yousuf Browser</h1>
                <p>Full Web Browser</p>
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
            # استخدام الخادم المحلي
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
        source_window.setWindowTitle("Page Source")
        source_window.setGeometry(150, 150, 1000, 600)
        
        text_edit = QTextEdit()
        text_edit.setPlainText(html)
        text_edit.setReadOnly(True)
        
        source_window.setCentralWidget(text_edit)
        source_window.show()
    
    def on_load_finished(self, success):
        if success:
            self.status.showMessage("Page loaded successfully")
        else:
            self.status.showMessage("Failed to load page")
    
    def on_load_progress(self, progress):
        self.status.showMessage(f"Loading... {progress}%")

if __name__ == "__main__":
    # تعطيل رسائل Qt
    os.environ["QT_LOGGING_RULES"] = "*.debug=false;qt.qpa.*=false"
    
    app = QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(True)
    
    initial_file = sys.argv[1] if len(sys.argv) > 1 else None
    window = YousufBrowserWindow(initial_file)
    window.show()
    sys.exit(app.exec_())
'''


def open_yousuf_browser(root=None):
    """
    فتح متصفح يوسف في عملية منفصلة
    
    Args:
        root: نافذة Tkinter الرئيسية (اختياري)
    
    Returns:
        None (يعمل في الخلفية)
    """
    
    def launch_browser_directly(filepath=None):
        """تشغيل المتصفح مباشرة في عملية منفصلة"""
        try:
            # حفظ كود المتصفح في ملف مؤقت
            temp_file = tempfile.NamedTemporaryFile(mode='w', suffix='.py', delete=False, encoding='utf-8')
            temp_file.write(BROWSER_CODE)
            temp_file.close()
            
            # تشغيل المتصفح مع إخفاء المخرجات
            python_exe = sys.executable
            
            # إنشاء عملية منفصلة تماماً
            if filepath:
                if os.name == 'nt':  # Windows
                    subprocess.Popen([python_exe, temp_file.name, filepath], 
                                   creationflags=subprocess.CREATE_NO_WINDOW)
                else:  # Linux/Mac
                    subprocess.Popen([python_exe, temp_file.name, filepath],
                                   stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL,
                                   start_new_session=True)
            else:
                if os.name == 'nt':  # Windows
                    subprocess.Popen([python_exe, temp_file.name],
                                   creationflags=subprocess.CREATE_NO_WINDOW)
                else:  # Linux/Mac
                    subprocess.Popen([python_exe, temp_file.name],
                                   stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL,
                                   start_new_session=True)
        except Exception as e:
            print(f"Error launching browser: {e}")
    
    # تشغيل المتصفح مباشرة بدون نافذة لانشر
    launch_browser_directly()
    
    return None


# للاختبار المستقل
if __name__ == "__main__":
    root = tk.Tk()
    root.withdraw()
    launcher = open_yousuf_browser(root)
    root.mainloop()