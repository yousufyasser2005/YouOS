import os
import sys
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, 
                              QPushButton, QSlider, QFrame, QFileDialog,
                              QScrollArea, QGridLayout, QTabWidget, QApplication)
from PyQt6.QtCore import Qt, QTimer, QSize
from PyQt6.QtGui import QPixmap, QIcon
import vlc

# GlassWidget base for consistency
try:
    from main import GlassWidget
except ImportError:
    from PyQt6.QtWidgets import QWidget as GlassWidget

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

MEDIA_PATH = os.path.expanduser("~/codes/os/user files/media files")
if not os.path.exists(MEDIA_PATH):
    MEDIA_PATH = os.path.expanduser("~")

IMAGE_EXT = ('.jpg', '.jpeg', '.png', '.gif', '.bmp', '.webp')
VIDEO_EXT = ('.mp4', '.avi', '.mkv', '.mov')
AUDIO_EXT = ('.mp3', '.wav', '.ogg', '.flac')


def get_files(extension_list):
    """Get files from media path with given extensions"""
    try:
        return [f for f in os.listdir(MEDIA_PATH) 
                if os.path.isfile(os.path.join(MEDIA_PATH, f)) 
                and f.lower().endswith(tuple(extension_list))]
    except Exception as e:
        print(f"Error getting files: {e}")
        return []


class MediaPlayer(QWidget):
    """VLC-based media player for video and audio"""
    def __init__(self, parent=None, path=None):
        super().__init__(parent)
        self.setWindowTitle("Media Player")
        self.resize(800, 600)
        
        self.setStyleSheet(f"""
            QWidget {{
                background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                    stop:0 rgba(15, 15, 30, 230),
                    stop:1 rgba(26, 26, 46, 230));
                border-radius: 12px;
                color: white;
            }}
            QPushButton {{
                background: {COLORS['bg_tertiary']};
                color: white;
                border: none;
                border-radius: 6px;
                padding: 8px 16px;
                font-size: 13px;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_primary']};
            }}
        """)
        
        self.instance = vlc.Instance()
        self.player = self.instance.media_player_new()
        self.filename = path
        self.slider_dragging = False

        self.setup_ui()
        
        if self.filename:
            self.load_media(self.filename)

    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(15, 15, 15, 15)
        layout.setSpacing(10)
        
        # Video Panel
        self.video_frame = QFrame()
        self.video_frame.setStyleSheet("background-color: black; border-radius: 8px;")
        self.video_frame.setMinimumHeight(400)
        layout.addWidget(self.video_frame, stretch=1)

        # Progress Slider
        self.progress_slider = QSlider(Qt.Orientation.Horizontal)
        self.progress_slider.setRange(0, 1000)
        self.progress_slider.setStyleSheet("""
            QSlider::groove:horizontal {
                background: rgba(255, 255, 255, 0.1);
                height: 6px;
                border-radius: 3px;
            }
            QSlider::handle:horizontal {
                background: #3b82f6;
                width: 14px;
                margin: -4px 0;
                border-radius: 7px;
            }
            QSlider::sub-page:horizontal {
                background: #3b82f6;
                border-radius: 3px;
            }
        """)
        self.progress_slider.sliderPressed.connect(lambda: setattr(self, 'slider_dragging', True))
        self.progress_slider.sliderReleased.connect(self.on_slider_release)
        layout.addWidget(self.progress_slider)

        # Controls
        controls_layout = QHBoxLayout()
        
        self.open_btn = QPushButton("Open")
        self.open_btn.clicked.connect(self.open_file)
        
        self.play_btn = QPushButton("▶ Play")
        self.play_btn.clicked.connect(self.play_media)
        
        self.pause_btn = QPushButton("⏸ Pause")
        self.pause_btn.clicked.connect(self.pause_media)
        
        self.stop_btn = QPushButton("⏹ Stop")
        self.stop_btn.clicked.connect(self.stop_media)
        
        self.vol_slider = QSlider(Qt.Orientation.Horizontal)
        self.vol_slider.setRange(0, 100)
        self.vol_slider.setValue(70)
        self.vol_slider.setFixedWidth(120)
        self.vol_slider.setStyleSheet(self.progress_slider.styleSheet())
        self.vol_slider.valueChanged.connect(self.set_volume)

        for btn in [self.open_btn, self.play_btn, self.pause_btn, self.stop_btn]:
            btn.setFixedHeight(35)
            controls_layout.addWidget(btn)
        
        controls_layout.addStretch()
        vol_label = QLabel("🔊")
        vol_label.setStyleSheet("color: white; font-size: 16px;")
        controls_layout.addWidget(vol_label)
        controls_layout.addWidget(self.vol_slider)
        
        layout.addLayout(controls_layout)

        self.timer = QTimer(self)
        self.timer.setInterval(500)
        self.timer.timeout.connect(self.update_ui)
        self.timer.start()

    def open_file(self):
        """Open file dialog to select media"""
        filename, _ = QFileDialog.getOpenFilename(
            self, "Select Media File", MEDIA_PATH,
            "Media Files (*.mp4 *.avi *.mkv *.mov *.mp3 *.wav *.ogg *.flac);;All Files (*.*)"
        )
        if filename:
            self.load_media(filename)

    def load_media(self, path):
        """Load media file into VLC player"""
        self.filename = path
        media = self.instance.media_new(path)
        self.player.set_media(media)
        
        if sys.platform.startswith("linux"):
            self.player.set_xwindow(int(self.video_frame.winId()))
        elif sys.platform == "win32":
            self.player.set_hwnd(int(self.video_frame.winId()))
        elif sys.platform == "darwin":
            self.player.set_nsobject(int(self.video_frame.winId()))
        
        self.set_volume(self.vol_slider.value())
        self.play_media()

    def play_media(self):
        """Play/resume media"""
        self.player.play()

    def pause_media(self):
        """Pause media"""
        self.player.pause()

    def stop_media(self):
        """Stop media playback"""
        self.player.stop()

    def set_volume(self, volume):
        """Set audio volume"""
        self.player.audio_set_volume(volume)

    def on_slider_release(self):
        """Handle slider release to seek"""
        self.slider_dragging = False
        position = self.progress_slider.value() / 1000.0
        self.player.set_position(position)

    def update_ui(self):
        """Update progress slider"""
        if not self.slider_dragging and self.player.is_playing():
            current_pos = self.player.get_position()
            if current_pos >= 0:
                self.progress_slider.setValue(int(current_pos * 1000))

    def closeEvent(self, event):
        """Clean up on close"""
        self.stop_media()
        self.timer.stop()
        event.accept()


class ImageViewer(QWidget):
    """Simple image viewer window"""
    def __init__(self, parent=None, image_path=None):
        super().__init__(parent)
        self.setWindowTitle(os.path.basename(image_path) if image_path else "Image Viewer")
        self.resize(900, 700)
        
        self.setStyleSheet(f"""
            QWidget {{
                background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                    stop:0 rgba(15, 15, 30, 230),
                    stop:1 rgba(26, 26, 46, 230));
                border-radius: 12px;
            }}
        """)
        
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setStyleSheet("background: transparent; border: none;")
        
        self.image_label = QLabel()
        self.image_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.image_label.setStyleSheet("background: rgba(0, 0, 0, 0.5); border-radius: 8px; padding: 10px;")
        
        if image_path and os.path.exists(image_path):
            pixmap = QPixmap(image_path)
            # Scale down if too large
            max_size = 800
            if pixmap.width() > max_size or pixmap.height() > max_size:
                pixmap = pixmap.scaled(max_size, max_size, 
                                      Qt.AspectRatioMode.KeepAspectRatio, 
                                      Qt.TransformationMode.SmoothTransformation)
            self.image_label.setPixmap(pixmap)
        
        scroll.setWidget(self.image_label)
        layout.addWidget(scroll)


class MediaViewer(QWidget):
    """Main media viewer with tabbed interface"""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Media Viewer")
        self.resize(900, 700)
        
        self.setStyleSheet(f"""
            QWidget {{
                background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                    stop:0 rgba(15, 15, 30, 230),
                    stop:1 rgba(26, 26, 46, 230));
                border-radius: 12px;
                color: white;
            }}
            QTabWidget::pane {{
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                background: transparent;
            }}
            QTabBar::tab {{
                background: {COLORS['bg_tertiary']};
                color: {COLORS['text_secondary']};
                padding: 10px 20px;
                margin-right: 2px;
                border-top-left-radius: 8px;
                border-top-right-radius: 8px;
            }}
            QTabBar::tab:selected {{
                background: {COLORS['accent_primary']};
                color: white;
            }}
            QPushButton {{
                background: {COLORS['bg_tertiary']};
                border: 1px solid {COLORS['border']};
                color: white;
                padding: 8px;
                border-radius: 6px;
                text-align: left;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
                border-color: {COLORS['accent_primary']};
            }}
            QScrollArea {{
                background: transparent;
                border: none;
            }}
        """)
        
        self.setup_ui()

    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(15, 15, 15, 15)
        
        self.tabs = QTabWidget()
        
        self.img_scroll = QScrollArea()
        self.img_scroll.setWidgetResizable(True)
        
        self.vid_scroll = QScrollArea()
        self.vid_scroll.setWidgetResizable(True)
        
        self.aud_scroll = QScrollArea()
        self.aud_scroll.setWidgetResizable(True)
        
        self.tabs.addTab(self.img_scroll, "📷 Images")
        self.tabs.addTab(self.vid_scroll, "🎬 Videos")
        self.tabs.addTab(self.aud_scroll, "🎵 Audio")
        
        layout.addWidget(self.tabs)
        
        self.load_media_content()

    def load_media_content(self):
        """Load media files into tabs"""
        img_widget = QWidget()
        img_widget.setStyleSheet("background: transparent;")
        img_grid = QGridLayout(img_widget)
        img_grid.setSpacing(15)
        
        image_files = get_files(IMAGE_EXT)
        for i, filename in enumerate(image_files):
            path = os.path.join(MEDIA_PATH, filename)
            btn = QPushButton()
            btn.setFixedSize(160, 160)
            btn.setStyleSheet(f"""
                QPushButton {{
                    background: {COLORS['bg_secondary']};
                    border: 2px solid {COLORS['border']};
                    border-radius: 8px;
                    padding: 5px;
                }}
                QPushButton:hover {{
                    border-color: {COLORS['accent_primary']};
                }}
            """)
            
            # Load thumbnail
            try:
                pixmap = QPixmap(path)
                if not pixmap.isNull():
                    pixmap = pixmap.scaled(150, 150, 
                                          Qt.AspectRatioMode.KeepAspectRatio, 
                                          Qt.TransformationMode.SmoothTransformation)
                    btn.setIcon(QIcon(pixmap))
                    btn.setIconSize(QSize(150, 150))
            except Exception as e:
                print(f"Error loading thumbnail for {filename}: {e}")
            
            btn.clicked.connect(lambda checked, p=path: self.view_image(p))
            img_grid.addWidget(btn, i // 4, i % 4)
        
        img_grid.addItem(QVBoxLayout(), img_grid.rowCount(), 0)  # Spacer
        self.img_scroll.setWidget(img_widget)

        vid_widget = QWidget()
        vid_widget.setStyleSheet("background: transparent;")
        vid_layout = QVBoxLayout(vid_widget)
        vid_layout.setSpacing(8)
        vid_layout.setContentsMargins(10, 10, 10, 10)
        
        video_files = get_files(VIDEO_EXT)
        for filename in video_files:
            path = os.path.join(MEDIA_PATH, filename)
            btn = QPushButton(f"🎬  {filename}")
            btn.setFixedHeight(45)
            btn.clicked.connect(lambda checked, p=path: self.play_media(p))
            vid_layout.addWidget(btn)
        
        vid_layout.addStretch()
        self.vid_scroll.setWidget(vid_widget)

        aud_widget = QWidget()
        aud_widget.setStyleSheet("background: transparent;")
        aud_layout = QVBoxLayout(aud_widget)
        aud_layout.setSpacing(8)
        aud_layout.setContentsMargins(10, 10, 10, 10)
        
        audio_files = get_files(AUDIO_EXT)
        for filename in audio_files:
            path = os.path.join(MEDIA_PATH, filename)
            btn = QPushButton(f"🎵  {filename}")
            btn.setFixedHeight(45)
            btn.clicked.connect(lambda checked, p=path: self.play_media(p))
            aud_layout.addWidget(btn)
        
        aud_layout.addStretch()
        self.aud_scroll.setWidget(aud_widget)

    def view_image(self, path):
        """Open image viewer window"""
        self.image_viewer = ImageViewer(self, path)
        self.image_viewer.show()

    def play_media(self, path):
        """Open media player window"""
        self.player = MediaPlayer(self, path)
        self.player.show()


def launch_media_viewer(parent=None, file_path=None):
    """Launch media viewer or player based on file type"""
    if file_path:
        ext = os.path.splitext(file_path)[1].lower()
        if ext in IMAGE_EXT:
            viewer = ImageViewer(parent, file_path)
        else:
            viewer = MediaPlayer(parent, file_path)
    else:
        viewer = MediaViewer(parent)
    
    viewer.show()
    return viewer


if __name__ == "__main__":
    app = QApplication(sys.argv)
    viewer = launch_media_viewer()
    sys.exit(app.exec())
