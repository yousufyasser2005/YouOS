"""
YouOS 10 - Face Recognition Module
face_recognition_module.py - Face recognition for biometric authentication
"""

import sys
import os

# CRITICAL FIX: Force user site-packages to load first (for opencv-contrib-python)
import site
user_site = site.getusersitepackages()
if user_site and user_site not in sys.path:
    sys.path.insert(0, user_site)

# Now import cv2 - should get the correct one
import cv2
import numpy as np
import pickle
from pathlib import Path

# Verify we have the face module
if not hasattr(cv2, 'face'):
    raise ImportError(
        f"OpenCV does not have 'face' module!\n"
        f"Location: {cv2.__file__}\n"
        f"Version: {cv2.__version__}\n\n"
        f"This means the system opencv is being loaded instead of opencv-contrib-python.\n"
        f"Solution: sudo apt remove python3-opencv"
    )

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, 
                              QPushButton, QDialog, QMessageBox)
from PyQt6.QtCore import Qt, QTimer, pyqtSignal
from PyQt6.QtGui import QImage, QPixmap

# Configuration
BASE_DIR = Path(__file__).parent
FACE_DATA_DIR = BASE_DIR / 'face_data'
FACE_DATA_DIR.mkdir(exist_ok=True)

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
    'warning': '#f59e0b',
}


class FaceRecognitionEngine:
    """Core face recognition engine using OpenCV and face_recognition library"""
    
    def __init__(self):
        self.face_cascade = None
        self.recognizer = None
        self.initialization_error = None
        
        # Debug: Print sys.path to see module search order
        print(f"\n=== FaceRecognitionEngine Init ===")
        print(f"sys.path (first 5):")
        for i, p in enumerate(sys.path[:5]):
            print(f"  [{i}] {p}")
        
        self.load_cascade()
        
    def load_cascade(self):
        """Load Haar Cascade for face detection"""
        try:
            # Debug: Print Python and OpenCV info
            import sys
            print(f"\n=== Face Recognition Module Init ===")
            print(f"Python executable: {sys.executable}")
            print(f"Python version: {sys.version}")
            print(f"OpenCV version: {cv2.__version__}")
            print(f"OpenCV file: {cv2.__file__}")
            print(f"Has face attribute: {hasattr(cv2, 'face')}")
            
            # Try to load OpenCV's face cascade
            cascade_path = cv2.data.haarcascades + 'haarcascade_frontalface_default.xml'
            self.face_cascade = cv2.CascadeClassifier(cascade_path)
            
            if self.face_cascade.empty():
                print("⚠️ Failed to load face cascade")
                return False
            
            # Initialize LBPH face recognizer - requires opencv-contrib-python
            try:
                self.recognizer = cv2.face.LBPHFaceRecognizer_create()
                print("✓ Face recognizer initialized successfully")
                self.initialization_error = None
            except AttributeError as e:
                error_msg = f"opencv-contrib-python not installed or face module unavailable\nError: {e}"
                print(f"❌ {error_msg}")
                import sys
                print(f"   Python: {sys.executable}")
                print(f"   OpenCV: {cv2.__version__}")
                self.recognizer = None
                self.initialization_error = error_msg
                return False
            except Exception as e:
                error_msg = f"Failed to create face recognizer: {e}"
                print(f"❌ {error_msg}")
                self.recognizer = None
                self.initialization_error = error_msg
                return False
            
            return True
        except Exception as e:
            print(f"❌ Error loading face cascade: {e}")
            import traceback
            traceback.print_exc()
            return False
    
    def detect_face(self, frame):
        """
        Detect face in frame
        Returns: (success, face_region, confidence)
        """
        if self.face_cascade is None:
            return False, None, 0
        
        # Convert to grayscale
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        
        # Detect faces
        faces = self.face_cascade.detectMultiScale(
            gray,
            scaleFactor=1.1,
            minNeighbors=5,
            minSize=(100, 100)
        )
        
        if len(faces) == 0:
            return False, None, 0
        
        # Get largest face
        largest_face = max(faces, key=lambda rect: rect[2] * rect[3])
        x, y, w, h = largest_face
        
        # Extract face region
        face_region = gray[y:y+h, x:x+w]
        
        # Calculate face quality/confidence
        confidence = self.calculate_face_quality(face_region)
        
        return True, face_region, confidence
    
    def calculate_face_quality(self, face_region):
        """Calculate face quality score (0-100)"""
        try:
            # Check sharpness using Laplacian
            laplacian_var = cv2.Laplacian(face_region, cv2.CV_64F).var()
            
            # Check brightness
            brightness = np.mean(face_region)
            
            # Calculate quality score
            sharpness_score = min(laplacian_var / 100, 1.0) * 50
            brightness_score = (1 - abs(brightness - 127) / 127) * 50
            
            quality = sharpness_score + brightness_score
            return int(quality)
        except:
            return 0
    
    def enroll_face(self, username, frames):
        """
        Enroll face from multiple frames
        frames: list of face regions (grayscale)
        """
        if len(frames) < 10:
            return False, "Not enough face samples"
        
        if self.recognizer is None:
            error_detail = self.initialization_error or "Unknown initialization error"
            return False, f"Face recognizer not initialized.\n\n{error_detail}\n\nTry restarting the application."
        
        try:
            # Prepare training data
            faces = []
            labels = []
            
            for face in frames:
                if face is None:
                    continue
                # Resize to standard size
                face_resized = cv2.resize(face, (200, 200))
                faces.append(face_resized)
                labels.append(0)  # Single user per model
            
            if len(faces) < 10:
                return False, "Not enough valid face samples"
            
            # Train recognizer
            self.recognizer.train(faces, np.array(labels))
            
            # Save model
            model_path = FACE_DATA_DIR / f"{username}_face.yml"
            self.recognizer.save(str(model_path))
            
            # Save metadata
            metadata = {
                'username': username,
                'num_samples': len(frames),
                'enrolled': True
            }
            
            metadata_path = FACE_DATA_DIR / f"{username}_metadata.pkl"
            with open(metadata_path, 'wb') as f:
                pickle.dump(metadata, f)
            
            return True, "Face enrolled successfully"
        except Exception as e:
            return False, f"Enrollment failed: {str(e)}"
    
    def recognize_face(self, username, face_region):
        """
        Recognize face against enrolled model
        Returns: (success, confidence)
        """
        model_path = FACE_DATA_DIR / f"{username}_face.yml"
        
        if not model_path.exists():
            return False, 0
        
        try:
            # Load trained model
            recognizer = cv2.face.LBPHFaceRecognizer_create()
            recognizer.read(str(model_path))
            
            # Resize face to match training size
            face_resized = cv2.resize(face_region, (200, 200))
            
            # Predict
            label, confidence = recognizer.predict(face_resized)
            
            # Lower confidence = better match (distance metric)
            # Typical threshold: confidence < 50 is good match
            match_confidence = max(0, 100 - confidence)
            
            # Consider it a match if confidence > 50
            is_match = match_confidence > 50
            
            return is_match, match_confidence
        except Exception as e:
            print(f"Recognition error: {e}")
            return False, 0
    
    def has_enrolled_face(self, username):
        """Check if user has enrolled face"""
        model_path = FACE_DATA_DIR / f"{username}_face.yml"
        return model_path.exists()
    
    def delete_enrolled_face(self, username):
        """Delete enrolled face data"""
        model_path = FACE_DATA_DIR / f"{username}_face.yml"
        metadata_path = FACE_DATA_DIR / f"{username}_metadata.pkl"
        
        try:
            if model_path.exists():
                model_path.unlink()
            if metadata_path.exists():
                metadata_path.unlink()
            return True
        except:
            return False


class FaceCameraWidget(QWidget):
    """Widget showing camera feed with face detection overlay"""
    
    face_detected = pyqtSignal(bool, int)  # (detected, quality)
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.camera = None
        self.timer = None
        self.engine = FaceRecognitionEngine()
        self.setup_ui()
        
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        
        # Camera view
        self.camera_label = QLabel()
        self.camera_label.setFixedSize(640, 480)
        self.camera_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.camera_label.setStyleSheet(f"""
            QLabel {{
                background: {COLORS['bg_tertiary']};
                border: 2px solid {COLORS['border']};
                border-radius: 12px;
            }}
        """)
        layout.addWidget(self.camera_label)
        
        # Status label
        self.status_label = QLabel("Initializing camera...")
        self.status_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.status_label.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 14px;
            padding: 10px;
        """)
        layout.addWidget(self.status_label)
    
    def start_camera(self):
        """Start camera capture"""
        try:
            self.camera = cv2.VideoCapture(0)
            
            if not self.camera.isOpened():
                self.status_label.setText("❌ Camera not available")
                self.status_label.setStyleSheet(f"color: {COLORS['error']}; font-size: 14px; padding: 10px;")
                return False
            
            # Set camera properties
            self.camera.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
            self.camera.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
            
            # Start update timer
            self.timer = QTimer()
            self.timer.timeout.connect(self.update_frame)
            self.timer.start(30)  # ~30 FPS
            
            self.status_label.setText("Position your face in the frame")
            self.status_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 14px; padding: 10px;")
            
            return True
        except Exception as e:
            self.status_label.setText(f"❌ Camera error: {str(e)}")
            self.status_label.setStyleSheet(f"color: {COLORS['error']}; font-size: 14px; padding: 10px;")
            return False
    
    def update_frame(self):
        """Update camera frame"""
        if self.camera is None or not self.camera.isOpened():
            return
        
        ret, frame = self.camera.read()
        if not ret:
            return
        
        # Flip frame horizontally (mirror effect)
        frame = cv2.flip(frame, 1)
        
        # Detect face
        detected, face_region, quality = self.engine.detect_face(frame)
        
        # Draw face detection overlay
        if detected:
            # Find face location in original frame
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            faces = self.engine.face_cascade.detectMultiScale(gray, 1.1, 5, minSize=(100, 100))
            
            if len(faces) > 0:
                # Draw rectangle around face
                x, y, w, h = faces[0]
                
                # Color based on quality
                if quality >= 70:
                    color = (0, 255, 0)  # Green - good
                    status = f"✓ Face detected - Quality: {quality}%"
                    status_color = COLORS['success']
                elif quality >= 50:
                    color = (0, 255, 255)  # Yellow - acceptable
                    status = f"⚠ Face detected - Quality: {quality}% (improve lighting)"
                    status_color = COLORS['warning']
                else:
                    color = (0, 0, 255)  # Red - poor
                    status = f"✗ Face quality too low: {quality}%"
                    status_color = COLORS['error']
                
                # Draw rectangle
                cv2.rectangle(frame, (x, y), (x+w, y+h), color, 3)
                
                # Draw corner markers
                corner_length = 30
                cv2.line(frame, (x, y), (x + corner_length, y), color, 5)
                cv2.line(frame, (x, y), (x, y + corner_length), color, 5)
                cv2.line(frame, (x + w, y), (x + w - corner_length, y), color, 5)
                cv2.line(frame, (x + w, y), (x + w, y + corner_length), color, 5)
                cv2.line(frame, (x, y + h), (x + corner_length, y + h), color, 5)
                cv2.line(frame, (x, y + h), (x, y + h - corner_length), color, 5)
                cv2.line(frame, (x + w, y + h), (x + w - corner_length, y + h), color, 5)
                cv2.line(frame, (x + w, y + h), (x + w, y + h - corner_length), color, 5)
                
                self.status_label.setText(status)
                self.status_label.setStyleSheet(f"color: {status_color}; font-size: 14px; padding: 10px; font-weight: bold;")
            
            self.face_detected.emit(True, quality)
        else:
            self.status_label.setText("⚠ Your face can't be detected. Please make sure your face is visible in the camera.")
            self.status_label.setStyleSheet(f"color: {COLORS['warning']}; font-size: 14px; padding: 10px;")
            self.face_detected.emit(False, 0)
        
        # Convert frame to QPixmap
        rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        h, w, ch = rgb_frame.shape
        bytes_per_line = ch * w
        qt_image = QImage(rgb_frame.data, w, h, bytes_per_line, QImage.Format.Format_RGB888)
        pixmap = QPixmap.fromImage(qt_image)
        
        # Display frame
        self.camera_label.setPixmap(pixmap)
    
    def get_current_face(self):
        """Get current face region from camera"""
        if self.camera is None or not self.camera.isOpened():
            return None, 0
        
        ret, frame = self.camera.read()
        if not ret:
            return None, 0
        
        frame = cv2.flip(frame, 1)
        detected, face_region, quality = self.engine.detect_face(frame)
        
        if detected and quality >= 60:
            return face_region, quality
        
        return None, quality
    
    def stop_camera(self):
        """Stop camera capture"""
        if self.timer:
            self.timer.stop()
        if self.camera:
            self.camera.release()
        self.camera = None


class FaceEnrollmentDialog(QDialog):
    """Dialog for enrolling face"""
    
    enrollment_complete = pyqtSignal(bool)
    
    def __init__(self, username, parent=None):
        super().__init__(parent)
        self.username = username
        self.face_samples = []
        self.required_samples = 20
        self.engine = FaceRecognitionEngine()
        self.setup_ui()
        
    def setup_ui(self):
        self.setWindowTitle(f"Enroll Face - {self.username}")
        self.setFixedSize(700, 650)
        self.setModal(True)
        self.setStyleSheet(f"background: {COLORS['bg_primary']}; color: {COLORS['text_primary']};")
        
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(15)
        
        # Title
        title = QLabel("Face Enrollment")
        title.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 20px; font-weight: bold;")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)
        
        # Instructions
        instructions = QLabel("Look at the camera and slowly move your head slightly in different directions.\nWe'll capture multiple angles for better recognition.")
        instructions.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        instructions.setAlignment(Qt.AlignmentFlag.AlignCenter)
        instructions.setWordWrap(True)
        layout.addWidget(instructions)
        
        # Camera widget
        self.camera_widget = FaceCameraWidget()
        self.camera_widget.face_detected.connect(self.on_face_detected)
        layout.addWidget(self.camera_widget)
        
        # Progress layout
        progress_layout = QHBoxLayout()
        
        self.progress_label = QLabel(f"Samples: 0/{self.required_samples}")
        self.progress_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 14px; font-weight: bold;")
        progress_layout.addWidget(self.progress_label)
        
        progress_layout.addStretch()
        
        layout.addLayout(progress_layout)
        
        # Buttons
        button_layout = QHBoxLayout()
        
        self.cancel_btn = QPushButton("Cancel")
        self.cancel_btn.setFixedHeight(40)
        self.cancel_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.cancel_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['bg_tertiary']};
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                padding: 0 30px;
                font-size: 14px;
            }}
            QPushButton:hover {{
                background: {COLORS['border']};
            }}
        """)
        self.cancel_btn.clicked.connect(self.reject)
        button_layout.addWidget(self.cancel_btn)
        
        button_layout.addStretch()
        
        self.enroll_btn = QPushButton("Start Enrollment")
        self.enroll_btn.setFixedHeight(40)
        self.enroll_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.enroll_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 8px;
                padding: 0 30px;
                font-size: 14px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
            }}
            QPushButton:disabled {{
                background: {COLORS['border']};
                color: {COLORS['text_secondary']};
            }}
        """)
        self.enroll_btn.clicked.connect(self.start_enrollment)
        button_layout.addWidget(self.enroll_btn)
        
        layout.addLayout(button_layout)
        
        # Start camera
        self.camera_widget.start_camera()
    
    def start_enrollment(self):
        """Start capturing face samples"""
        self.enroll_btn.setEnabled(False)
        self.enroll_btn.setText("Capturing...")
        
        # Start capture timer
        self.capture_timer = QTimer()
        self.capture_timer.timeout.connect(self.capture_sample)
        self.capture_timer.start(500)  # Capture every 500ms
    
    def capture_sample(self):
        """Capture a face sample"""
        face, quality = self.camera_widget.get_current_face()
        
        if face is not None and quality >= 60:
            self.face_samples.append(face)
            self.progress_label.setText(f"Samples: {len(self.face_samples)}/{self.required_samples}")
            
            # Play capture sound (optional)
            try:
                from utils import play_sound
                play_sound('click.wav')
            except:
                pass
            
            # Check if we have enough samples
            if len(self.face_samples) >= self.required_samples:
                if self.capture_timer:
                    self.capture_timer.stop()
                self.complete_enrollment()
    
    def complete_enrollment(self):
        """Complete enrollment process"""
        self.enroll_btn.setText("Processing...")
        
        # Debug: Check engine state
        print(f"\n=== ENROLLMENT DEBUG ===")
        print(f"Engine: {self.engine}")
        print(f"Recognizer: {self.engine.recognizer}")
        print(f"Face cascade: {self.engine.face_cascade}")
        print(f"Init error: {self.engine.initialization_error}")
        print(f"Samples collected: {len(self.face_samples)}")
        print(f"========================\n")
        
        # Enroll face
        success, message = self.engine.enroll_face(self.username, self.face_samples)
        
        if success:
            QMessageBox.information(self, "Success", "Face enrolled successfully!\nYou can now use biometric unlock.")
            self.enrollment_complete.emit(True)
            self.accept()
        else:
            QMessageBox.critical(self, "Error", f"Enrollment failed: {message}")
            self.enrollment_complete.emit(False)
            self.reject()
    
    def on_face_detected(self, detected, quality):
        """Handle face detection updates"""
        pass  # Real-time feedback already handled in camera widget
    
    def closeEvent(self, event):
        """Clean up on close"""
        self.camera_widget.stop_camera()
        super().closeEvent(event)


class FaceAuthenticationDialog(QDialog):
    """Dialog for face authentication on login"""
    
    authentication_complete = pyqtSignal(bool)
    
    def __init__(self, username, parent=None):
        super().__init__(parent)
        self.username = username
        self.engine = FaceRecognitionEngine()
        self.attempting_auth = False
        self.setup_ui()
        
    def setup_ui(self):
        self.setWindowTitle(f"Face Authentication - {self.username}")
        self.setFixedSize(700, 600)
        self.setModal(True)
        self.setStyleSheet(f"background: {COLORS['bg_primary']}; color: {COLORS['text_primary']};")
        
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(15)
        
        # Title
        title = QLabel("Face Authentication")
        title.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 20px; font-weight: bold;")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)
        
        # Instructions
        instructions = QLabel(f"Please look at the camera to authenticate as {self.username}")
        instructions.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 13px;")
        instructions.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(instructions)
        
        # Camera widget
        self.camera_widget = FaceCameraWidget()
        self.camera_widget.face_detected.connect(self.on_face_detected)
        layout.addWidget(self.camera_widget)
        
        # Status
        self.auth_status = QLabel("")
        self.auth_status.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 14px;")
        self.auth_status.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.auth_status)
        
        # Buttons
        button_layout = QHBoxLayout()
        
        self.cancel_btn = QPushButton("Cancel")
        self.cancel_btn.setFixedHeight(40)
        self.cancel_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.cancel_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['bg_tertiary']};
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                padding: 0 30px;
                font-size: 14px;
            }}
            QPushButton:hover {{
                background: {COLORS['border']};
            }}
        """)
        self.cancel_btn.clicked.connect(self.reject)
        button_layout.addWidget(self.cancel_btn)
        
        button_layout.addStretch()
        
        self.auth_btn = QPushButton("Authenticate")
        self.auth_btn.setFixedHeight(40)
        self.auth_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.auth_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['success']};
                color: white;
                border: none;
                border-radius: 8px;
                padding: 0 30px;
                font-size: 14px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: #059669;
            }}
            QPushButton:disabled {{
                background: {COLORS['border']};
                color: {COLORS['text_secondary']};
            }}
        """)
        self.auth_btn.clicked.connect(self.attempt_authentication)
        button_layout.addWidget(self.auth_btn)
        
        layout.addLayout(button_layout)
        
        # Start camera
        self.camera_widget.start_camera()
    
    def attempt_authentication(self):
        """Attempt face authentication"""
        if self.attempting_auth:
            return
        
        self.attempting_auth = True
        self.auth_btn.setEnabled(False)
        self.auth_btn.setText("Authenticating...")
        self.auth_status.setText("Verifying face...")
        
        # Get current face
        face, quality = self.camera_widget.get_current_face()
        
        if face is None:
            self.auth_status.setText("❌ No face detected")
            self.auth_status.setStyleSheet(f"color: {COLORS['error']}; font-size: 14px; font-weight: bold;")
            self.auth_btn.setEnabled(True)
            self.auth_btn.setText("Authenticate")
            self.attempting_auth = False
            return
        
        # Recognize face
        is_match, confidence = self.engine.recognize_face(self.username, face)
        
        if is_match:
            self.auth_status.setText(f"✓ Authentication successful! (Confidence: {confidence:.1f}%)")
            self.auth_status.setStyleSheet(f"color: {COLORS['success']}; font-size: 14px; font-weight: bold;")
            
            # Play success sound
            
            QTimer.singleShot(1000, lambda: self.complete_authentication(True))
        else:
            self.auth_status.setText(f"✗ Face not recognized (Confidence: {confidence:.1f}%)")
            self.auth_status.setStyleSheet(f"color: {COLORS['error']}; font-size: 14px; font-weight: bold;")
            self.auth_btn.setEnabled(True)
            self.auth_btn.setText("Try Again")
            self.attempting_auth = False
            
            # Play error sound
            try:
                from utils import play_sound
                play_sound('error.wav')
            except:
                pass
    
    def complete_authentication(self, success):
        """Complete authentication process"""
        self.authentication_complete.emit(success)
        if success:
            self.accept()
        else:
            self.reject()
    
    def on_face_detected(self, detected, quality):
        """Handle face detection updates"""
        if detected and quality >= 60 and not self.attempting_auth:
            self.auth_btn.setEnabled(True)
        elif not self.attempting_auth:
            self.auth_btn.setEnabled(False)
    
    def closeEvent(self, event):
        """Clean up on close"""
        self.camera_widget.stop_camera()
        super().closeEvent(event)