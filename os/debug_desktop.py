import sys
sys.path.insert(0, '.')

from PyQt6.QtWidgets import QApplication, QMainWindow, QWidget
from PyQt6.QtCore import QTimer

app = QApplication(sys.argv)

from main import AuthManager
from desktop import DesktopManager

auth = AuthManager()
w = QMainWindow()
desktop = DesktopManager(auth, 'admin', w)
w.setCentralWidget(desktop)
w.showFullScreen()

def check():
    print('=== GEOMETRY REPORT ===')
    print(f'Window:       {w.geometry()}')
    print(f'Desktop:      {desktop.geometry()}')
    print(f'Taskbar:      {desktop.taskbar.geometry()}')
    print(f'Taskbar global top-left: {desktop.taskbar.mapToGlobal(desktop.taskbar.rect().topLeft())}')
    print(f'Widget panel: {desktop.widget_panel.geometry()}')
    print()
    print('=== ALL VISIBLE DIRECT CHILDREN ===')
    for child in desktop.children():
        if isinstance(child, QWidget) and child.isVisible():
            g = child.geometry()
            print(f'  {child.__class__.__name__:30s} y={g.y():4d}  h={g.height():4d}  bottom={g.y()+g.height():4d}')
    app.quit()

QTimer.singleShot(1500, check)
app.exec()
